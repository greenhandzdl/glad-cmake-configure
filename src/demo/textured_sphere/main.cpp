/**
 * @file main.cpp
 * @brief textured_sphere - a UV sphere with a live-tunable texture and *two*
 *        kinds of subdivision: one on the CPU, one in the tessellation stages.
 *
 * Three layers of the library meet here, each kept visible rather than hidden:
 *
 *   GeometryFactory::Sphere(radius, segments, rings) builds the MeshData on the
 *   CPU (Stage A) - the two integers are the meridian / parallel counts, and the
 *   HUD prints the vertex / index counts they produce, so the cost of "smoother"
 *   is a number rather than a feeling. Mesh::Update re-uploads the buffers in
 *   place while keeping the same VAO, which is what makes re-tesselling on every
 *   key press cheap enough to be interactive.
 *
 *   The same MeshData a second time, drawn as GL_PATCHES through a tessellation
 *   control / evaluation pair (TCS + TES). This is the part the CPU loop cannot
 *   do: the cage stays coarse, the control stage decides how many times to cut
 *   each edge, and the evaluation stage re-projects the generated point back onto
 *   the sphere - so a 12x6 cage renders as a round surface with a few hundred
 *   generated triangles per patch and no extra CPU vertices at all. `G` switches
 *   between the two paths over one mesh, which is the only honest way to compare
 *   them: the patch stream is the *same* index buffer, three indices per patch.
 *
 *   Texture2D::Upload takes a CPU Texture2DDesc (Stage B, render thread). All
 *   three maps here are generated in code, not loaded from disk: an equirectangular
 *   UV grid (u ramps red -> green, v ramps blue -> yellow, plus a line every 10%
 *   of each axis), a colourful checker, and a noise "planet" map. Because a UV
 *   sphere's texture coordinates are just s/segments and r/rings, that grid is the
 *   honest way to *see* which subdivision is doing the work: with the CPU mesh the
 *   meridians snap to the segment columns and every pole row collapses into the same
 *   two triangles, while the tessellated path spreads its lattice over the same
 *   parameterised domain at a density the CPU never paid for.
 *
 *   The programs are hand-written GLSL 410 rather than GeometryPass / PBR:
 *   location 0 position, 1 normal, 2 uv (the layout gldx::Vertex fixes), a single
 *   half-lambert term so the texture stays readable, one sampler2D on unit 0.
 *
 *   Two things this pass has to get right that the API never reports:
 *
 *   - The sRGB round-trip of any pass that draws straight to the window: the maps
 *     are uploaded as sRGB, so sampling yields linear radiance, and the value
 *     written to the (non-sRGB) default framebuffer has to be encoded again -
 *     skip it and a correctly-written albedo comes out muddy and dark.
 *   - Winding. Renderer::Init() sets glFrontFace(GL_CCW) + glCullFace(GL_BACK), so
 *     a mesh whose triangles are clockwise as seen from outside has its entire
 *     visible skin classified as a back face. Nothing warns you; you just draw the
 *     inside of the far side. `C` turns culling off and lets the fragment stage dim
 *     back faces, which is exactly how you *find* an inverted mesh.
 *
 * Controls: drag orbits, wheel zooms, [ / ] (or Left / Right) change segments,
 * - / + (or Up / Down) change rings, G toggles GPU tessellation, 1-9 set the
 * tessellation level, A toggles screen-space-adaptive levels, C toggles back-face
 * culling, T cycles the texture, W toggles wireframe (over a tessellated cage this
 * is the picture that shows the generated lattice and its seams), H toggles the HUD,
 * Esc quits.
 *
 * Headless: --quit-after SECONDS, and --snapshot PATH [--snap-at SECONDS] dumps a
 * PNG and closes, so a sweep can diff the tessellation visually. Initial values
 * come from --segments N --rings N --texture uv|checker|planet --tess-level N
 * --on tess --on adaptive --off cull --on wireframe --sphere-radius R --yaw A
 * --pitch B --orbit D.
 */

#include "gldx/core/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

import gldx;
import gldxwin;
import gldxcli;

namespace {

// Everything the frame callback reads, mutated by the input callbacks. One
// struct, captured by reference, so no callback has to reach into another's
// state - the same shape the other single-file demos use.
struct View {
    int   segments = 48;          // meridians  ([ / ] or Left / Right)
    int   rings    = 24;          // parallels   (- / + or Up / Down)
    float radius   = 1.6f;        // sphere radius, not the orbit distance
    int   texture  = 0;           // 0 uv grid, 1 checker, 2 planet (T cycles)
    bool  tess     = false;       // G: draw the cage as GL_PATCHES through TCS/TES
    int   tessLevel = 3;          // 1-9: subdivisions (uniform) / detail scale
    bool  adaptive = false;       // A: level from the edge's screen length
    bool  cull     = true;        // C: back-face culling, for reading the winding
    bool  wireframe = false;      // W toggles
    bool  hud       = true;       // H toggles
    bool  dirty     = true;       // geometry needs re-upload next frame
    float yaw = 0.6f, pitch = 0.25f, orbit = 4.2f;
    double lastX = 0.0, lastY = 0.0;
    bool  dragging = false;
};

constexpr int kMinSegments = 4;
constexpr int kMaxSegments = 256;
constexpr int kMinRings    = 2;
constexpr int kMaxRings    = 128;
constexpr int kMinTessLevel = 1;
constexpr int kMaxTessLevel = 9;      // one key press per level, 1..9
constexpr int kTextureCount = 3;
// One patch per existing triangle: the sphere's index stream is already three
// indices per primitive, so GL_PATCHES needs no second copy of the geometry.
constexpr GLint kPatchVertices = 3;

const char* const kTextureNames[kTextureCount] = {"uv grid", "checker", "planet"};
// CLI spelling of the same three maps (--texture uv|checker|planet).
const char* const kTextureTokens[kTextureCount] = {"uv", "checker", "planet"};

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

glm::vec3 Ramp(float t, glm::vec3 a, glm::vec3 b) {
    return a + (b - a) * t;
}

// Equirectangular UV grid: the hue encodes the coordinate pair, so a texel's
// colour tells you where (u,v) it is - the only map that lets you read the
// sphere's parameterisation off the shading. Lines every 10% of each axis turn
// into the meridian / parallel ladder when wrapped.
gldx::Texture2DDesc MakeUvGridDesc(int size = 512) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 3;
    d.srgb = true;
    d.pixels.resize(static_cast<std::size_t>(size) * size * 3);
    for (int y = 0; y < size; ++y) {
        const float v = static_cast<float>(y) / (size - 1);
        for (int x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / (size - 1);
            glm::vec3 c = Ramp(u, glm::vec3(0.90f, 0.25f, 0.20f),   // red  -> green
                                   glm::vec3(0.20f, 0.80f, 0.35f));
            c = Ramp(v, c, glm::vec3(c.r, c.g * 0.6f, 0.95f));       // -> blue tint
            const int gx = x * 10 / size, gy = y * 10 / size;        // decile grid
            const bool line = (x % (size / 10) == 0) || (y % (size / 10) == 0);
            if (line) c = (gx + gy) % 2 == 0 ? glm::vec3(0.06f) : glm::vec3(0.96f);
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            d.pixels[i + 0] = static_cast<std::uint8_t>(c.r * 255.0f);
            d.pixels[i + 1] = static_cast<std::uint8_t>(c.g * 255.0f);
            d.pixels[i + 2] = static_cast<std::uint8_t>(c.b * 255.0f);
        }
    }
    return d;
}

// Plain colour checker with a marker in the [0,0.5]x[0,0.5] quadrant, so the
// wrap direction and the v origin stay readable on the surface.
gldx::Texture2DDesc MakeCheckerDesc(int size = 512) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 3;
    d.srgb = true;
    d.pixels.resize(static_cast<std::size_t>(size) * size * 3);
    const int cells = 16;
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool on = ((x / cell) + (y / cell)) % 2 == 0;
            glm::vec3 c = on ? glm::vec3(0.92f, 0.86f, 0.72f) : glm::vec3(0.16f, 0.28f, 0.46f);
            if (x < size / 2 && y < size / 2) c = on ? glm::vec3(0.90f, 0.30f, 0.25f)
                                                    : glm::vec3(0.35f, 0.12f, 0.15f);
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            d.pixels[i + 0] = static_cast<std::uint8_t>(c.r * 255.0f);
            d.pixels[i + 1] = static_cast<std::uint8_t>(c.g * 255.0f);
            d.pixels[i + 2] = static_cast<std::uint8_t>(c.b * 255.0f);
        }
    }
    return d;
}

// Noise "planet" map. The noise is sampled on a circle in x, so the left and
// right edges of the map meet continuously - the vertical seam a naive
// u-scaled noise always leaves on a sphere. Land / sea / ice-cap thresholds are
// fixed, so two runs with the same seed are pixel-identical.
gldx::Texture2DDesc MakePlanetDesc(int size = 512) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 3;
    d.srgb = true;
    d.pixels.resize(static_cast<std::size_t>(size) * size * 3);
    const gldx::Noise noise(20260922u);
    const glm::vec3 ocean(0.16f, 0.38f, 0.62f), shelf(0.26f, 0.58f, 0.68f);
    const glm::vec3 grass(0.36f, 0.62f, 0.30f), highland(0.66f, 0.56f, 0.38f);
    const glm::vec3 ice(0.94f, 0.96f, 0.98f);
    for (int y = 0; y < size; ++y) {
        const float v = static_cast<float>(y) / (size - 1);
        for (int x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / size;
            const double a = 2.0 * glm::pi<double>() * u;
            const double h = noise.Fbm3(std::cos(a) * 2.2, std::sin(a) * 2.2,
                                        (v - 0.5) * 4.0, 5);
            const float polar = std::abs(v - 0.5f) * 2.0f;      // 0 equator -> 1 pole
            glm::vec3 c;
            if (h < -0.08)      c = h < -0.22 ? ocean : shelf;
            else if (h < 0.14)  c = grass;
            else                c = highland;
            if (polar > 0.86f)  c = ice;
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            d.pixels[i + 0] = static_cast<std::uint8_t>(c.r * 255.0f);
            d.pixels[i + 1] = static_cast<std::uint8_t>(c.g * 255.0f);
            d.pixels[i + 2] = static_cast<std::uint8_t>(c.b * 255.0f);
        }
    }
    return d;
}

const char* const kVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vNormal;
out vec2 vUv;
out vec3 vWorldPos;

void main() {
    vUv       = aUv;
    vWorldPos = (uModel * vec4(aPosition, 1.0)).xyz;
    vNormal   = mat3(uModel) * aNormal;
    gl_Position = uViewProj * vec4(vWorldPos, 1.0);
}
)GLSL";

// One fixed light + one texture unit. The half-lambert keeps the shaded side
// bright enough to read the map, a thin Blinn highlight makes the ball read as
// curved, and the decile grid of the uv texture does the rest of the explaining.
// Shared by both programs, so the only difference between the CPU and the
// tessellated path is *who produced the vertex*, not how it is shaded.
const char* const kFragment = R"GLSL(#version 410 core
in vec3 vNormal;
in vec2 vUv;
in vec3 vWorldPos;

uniform sampler2D uTex;
uniform vec3 uEye;

out vec4 FragColor;

void main() {
    // No `const` on these: GLSL 410 requires a const initialiser to be a
    // constant expression, so a normalise() result has to be a plain variable.
    vec3 n = normalize(vNormal);
    vec3 toEye  = normalize(uEye - vWorldPos);
    vec3 sunDir = normalize(vec3(-0.45, 0.85, 0.4));
    float ndl   = dot(n, sunDir) * 0.5 + 0.5;             // half-lambert
    float shade = 0.45 + 0.55 * ndl;                       // keep the map readable
    vec3  halfV = normalize(sunDir + toEye);
    float spec  = pow(max(dot(n, halfV), 0.0), 28.0) * 0.22;   // a hint of gloss
    vec3 albedo = texture(uTex, vUv).rgb;
    vec3 c = albedo * shade + vec3(spec);
    if (gl_FrontFacing == false) c *= 0.55;               // back faces read as inner shell
    // The maps are uploaded sRGB, so this sample is linear radiance, and the
    // default framebuffer is *not* sRGB (glfw only asks for one when
    // GLFW_SRGB_CAPABLE is set). Lighting happens in linear, then the result is
    // encoded back for display - skip this and the ball comes out muddy dark.
    FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// The tessellation path. Four stages instead of two, over the one MeshData:
//   patch vertex -> TCS (how often to cut each edge) -> TES (where the cut point
//   actually is) -> the fragment shader above.
// It is a separate program rather than one program with the pair optional,
// because the vertex stage of a patch pipeline must not write the final gl_Position
// (the evaluation stage owns it), so the two paths genuinely cannot share one VS.
// ---------------------------------------------------------------------------

// 1. PATCH VERTEX: forwards the cage. No projection here beyond what the control
//    stage needs to measure an edge in screen space.
const char* const kPatchVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;

uniform mat4 uModel;

out vec3 cObject;      // the cage point, which the TES re-projects
out vec3 cWorld;       // same point transformed, for edge measurement only
out vec3 cNormal;
out vec2 cUv;

void main() {
    cObject = aPosition;
    cWorld  = (uModel * vec4(aPosition, 1.0)).xyz;
    cNormal = aNormal;
    cUv     = aUv;
    // Ignored: with a TCS/TES pair in the program and the draw in GL_PATCHES, the
    // evaluation stage is the one that writes gl_Position.
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
)GLSL";

// 2. TESSELLATION CONTROL: declares the patch as one triangle of the existing
//    index stream and sets the levels. Runs once per control point (3x per patch,
//    each invocation writing its own slot), which is why the copy-out below uses
//    gl_InvocationID instead of a loop.
const char* const kTessControl = R"GLSL(#version 410 core
layout(vertices = 3) out;

in vec3 cObject[];
in vec3 cWorld[];
in vec3 cNormal[];
in vec2 cUv[];

uniform mat4  uViewProj;
uniform vec2  uViewport;    // pixels, to turn an edge into a screen length
uniform float uTessLevel;   // the 1-9 knob: subdivisions, or detail per 48 px
uniform float uMaxLevel;    // min(GL_MAX_TESS_GEN_LEVEL, 24), read once on the CPU
uniform int   uAdaptive;    // 0 => every patch gets uTessLevel, 1 => per-edge below

out PatchVertex {
    vec3 object;
    vec3 normal;
    vec2 uv;
} vc[];

// Subdivisions for the world-space edge a-b. Symmetric in a and b, and that symmetry
// is the entire trick of a crack-free adaptive level: two neighbouring patches sharing
// this edge feed the same two points into the same function, so they must agree on the
// level - and equal levels on a shared edge are what make the tessellator generate the
// same points on it from both sides. An asymmetric function (say one that prefers
// a.x) opens a visible T-junction crack along every seam.
float EdgeLevel(vec3 a, vec3 b) {
    vec4 pa = uViewProj * vec4(a, 1.0);
    vec4 pb = uViewProj * vec4(b, 1.0);
    if (pa.w <= 0.0 || pb.w <= 0.0) return 1.0;   // behind the eye: no detail needed
    vec2 sa = (pa.xy / pa.w) * 0.5 * uViewport;   // NDC -> pixels
    vec2 sb = (pb.xy / pb.w) * 0.5 * uViewport;
    return clamp(uTessLevel * length(sb - sa) / 48.0, 1.0, uMaxLevel);
}

void main() {
    float l0 = EdgeLevel(cWorld[1], cWorld[2]);
    float l1 = EdgeLevel(cWorld[2], cWorld[0]);
    float l2 = EdgeLevel(cWorld[0], cWorld[1]);
    // outer[i] is the edge *opposite* control point i, hence the pairings above.
    gl_TessLevelOuter[0] = uAdaptive == 1 ? l0 : uTessLevel;
    gl_TessLevelOuter[1] = uAdaptive == 1 ? l1 : uTessLevel;
    gl_TessLevelOuter[2] = uAdaptive == 1 ? l2 : uTessLevel;
    // The inner level only moves points that are not on an edge, so neighbours may
    // disagree on it without opening a crack. Averaging the three edges keeps the
    // interior as dense as the boundary; leaving it at 1 would hollow the patch out
    // into a ring of triangles around an empty middle.
    gl_TessLevelInner[0] = uAdaptive == 1 ? (l0 + l1 + l2) / 3.0 : uTessLevel;

    vc[gl_InvocationID].object = cObject[gl_InvocationID];
    vc[gl_InvocationID].normal = cNormal[gl_InvocationID];
    vc[gl_InvocationID].uv     = cUv[gl_InvocationID];
}
)GLSL";

// 3. TESSELLATION EVALUATION: the stage that actually turns a coarse cage into a
//    round surface. gl_TessCoord is the barycentric triple of the generated point,
//    so the body is one weighted average and a re-projection.
const char* const kTessEval = R"GLSL(#version 410 core
// equal_spacing puts edge points at exact fractions of the edge, which is what makes
// the shared-edge argument in EdgeLevel() hold. fractional_*_spacing would let the
// density breathe more smoothly between patches, but matching outer levels alone are
// then not always enough to keep the seam watertight.
// ccw matches how GeometryFactory::Sphere winds its triangles: control points run
// counter-clockwise as seen from outside, so the generated triangles come out facing
// the same way and the renderer's GL_BACK cull still keeps the far side off.
layout(triangles, equal_spacing, ccw) in;

uniform mat4  uModel;
uniform mat4  uViewProj;
uniform float uRadius;

in PatchVertex {
    vec3 object;
    vec3 normal;
    vec2 uv;
} vc[];

out vec3 vNormal;
out vec2 vUv;
out vec3 vWorldPos;

void main() {
    // Interpolate over the *cage*. A straight edge between two points on a sphere
    // cuts the corner, so this average lies inside the surface - how far inside is
    // exactly the faceting a CPU mesh would have to add vertices to hide.
    vec3 p = gl_TessCoord.x * vc[0].object
           + gl_TessCoord.y * vc[1].object
           + gl_TessCoord.z * vc[2].object;
    // Pushing it back out to the radius is the payoff: the sphere is round at the
    // generated point's density, not at the cage's. On a sphere centred on the
    // origin the re-projected direction *is* the normal, so it is derived here
    // rather than interpolated - the coarse cage's own normals would only blur it.
    vec3 objectNormal = normalize(p);
    vec4 world = uModel * vec4(objectNormal * uRadius, 1.0);
    vUv         = gl_TessCoord.x * vc[0].uv + gl_TessCoord.y * vc[1].uv
                + gl_TessCoord.z * vc[2].uv;
    vNormal     = mat3(uModel) * objectNormal;
    vWorldPos   = world.xyz;
    gl_Position = uViewProj * world;
}
)GLSL";

// The sphere itself: owns the two programs, the texture, the mesh, and the decision
// of *when* to rebuild it. Rebuild is driven by view.dirty rather than by the
// key handlers, so input stays off the GL path and remeshing happens with the
// context current, right before the draw that needs it.
class SpherePass : public gldx::RenderPass {
public:
    explicit SpherePass(View& view) : RenderPass("Sphere"), view_(view) {
        auto program = gldx::ShaderProgram::CreateFromSource(kVertex, kFragment);
        if (!program) {
            std::fprintf(stderr, "textured_sphere shader error:\n%s\n", program.error().c_str());
            return;
        }
        program_ = std::make_unique<gldx::ShaderProgram>(std::move(*program));
        program_->Use();
        program_->Set("uTex", 0);

        // The patch pipeline is the optional half of the demo: a failed link here
        // costs the G mode, not the window, so a driver without the pair still runs
        // the CPU-mesh path and the CI sweep stays green.
        auto tess = gldx::ShaderProgram::CreateFromSources({
            {gldx::ShaderStage::Vertex,         kPatchVertex},
            {gldx::ShaderStage::TessControl,    kTessControl},
            {gldx::ShaderStage::TessEvaluation, kTessEval},
            {gldx::ShaderStage::Fragment,       kFragment},
        });
        if (!tess) {
            std::fprintf(stderr, "textured_sphere: tessellation program unavailable:\n%s\n",
                         tess.error().c_str());
            view_.tess = false;
            return;
        }
        tessProgram_ = std::make_unique<gldx::ShaderProgram>(std::move(*tess));
        tessProgram_->Use();
        tessProgram_->Set("uTex", 0);
        // The control stage clamps against this instead of against a number the demo
        // invented: MAX_TESS_GEN_LEVEL is the driver's promise, and exceeding it is
        // undefined rather than merely dropped.
        GLint genLevel = 0;
        glGetIntegerv(GL_MAX_TESS_GEN_LEVEL, &genLevel);
        maxTessLevel_ = std::min(genLevel > 0 ? static_cast<float>(genLevel) : 1.0f, 24.0f);
    }

    void Execute(gldx::RenderFrame& frame) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, frame.fbWidth, frame.fbHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        // Owned here instead of inherited from Renderer::Init(), for the same reason
        // the other direct-to-window passes re-assert depth: an earlier pass is free
        // to have left the cull state off, and C toggles it on purpose.
        glFrontFace(GL_CCW);
        glCullFace(GL_BACK);
        if (view_.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        glClearColor(0.10f, 0.11f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!program_) return;

        // Build before the validity check: on the very first frame the mesh does
        // not exist yet, and this is the pass that creates it.
        if (view_.dirty) remesh();
        if (loadedTexture_ != view_.texture) {
            // Texture2D::Upload replaces the GL object, so cycling maps with T is
            // a full re-upload - cheap here (512^2) and the only honest way to
            // swap a map without an asset cache in the picture.
            uploadTexture(view_.texture);
            loadedTexture_ = view_.texture;
        }
        if (!mesh_.valid()) return;

        const bool tessOn = view_.tess && tessProgram_;
        const float cp = std::cos(view_.pitch);
        const glm::vec3 eye(view_.orbit * cp * std::sin(view_.yaw),
                            view_.orbit * std::sin(view_.pitch),
                            view_.orbit * cp * std::cos(view_.yaw));
        const float aspect = frame.fbHeight > 0
                                 ? static_cast<float>(frame.fbWidth) / static_cast<float>(frame.fbHeight)
                                 : 1.0f;
        const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(50.0f), aspect, 0.1f, 100.0f);
        const glm::mat4 model = glm::mat4(1.0f);

        gldx::ShaderProgram& prog = tessOn ? *tessProgram_ : *program_;
        prog.Use();
        prog.Set("uModel", model);
        prog.Set("uViewProj", proj * view);
        prog.Set("uEye", eye);
        if (tessOn) {
            prog.Set("uRadius", view_.radius);
            prog.Set("uTessLevel", static_cast<float>(view_.tessLevel));
            prog.Set("uMaxLevel", maxTessLevel_);
            prog.Set("uAdaptive", view_.adaptive ? 1 : 0);
            prog.Set("uViewport", glm::vec2(static_cast<float>(frame.fbWidth),
                                            static_cast<float>(frame.fbHeight)));
        }
        texture_.Bind(0);

        if (view_.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        if (tessOn) {
            // Patch configuration is pass-level GL (state on the context, not on the
            // VAO or the program), so it is called raw here - same as shader_stages.
            glPatchParameteri(GL_PATCH_VERTICES, kPatchVertices);
            mesh_.DrawAs(GL_PATCHES);
        } else {
            mesh_.Draw();
        }
        if (view_.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    [[nodiscard]] std::size_t vertices() const noexcept { return mesh_.vertexCount(); }
    [[nodiscard]] std::size_t indices()  const noexcept { return mesh_.indexCount(); }
    // One patch per source triangle, so the patch count is the index count / 3 - the
    // number the tessellator multiplies by its level^2 to get the real workload.
    [[nodiscard]] std::size_t patches()  const noexcept { return mesh_.indexCount() / kPatchVertices; }
    [[nodiscard]] bool tessAvailable() const noexcept { return tessProgram_ != nullptr; }
    // False when the program never linked: main then quits with a non-zero code
    // instead of showing an empty window, so a CI sweep sees the failure.
    [[nodiscard]] bool ok() const noexcept { return program_ != nullptr; }

private:
    void remesh() {
        // Stage A on the render thread here (a few thousand vertices - the point
        // of the demo is the interactive loop, not off-thread generation);
        // Update keeps the VAO and its attribute wiring, replacing only bytes.
        gldx::MeshData data = gldx::GeometryFactory::Sphere(
            view_.radius, view_.segments, view_.rings);
        if (mesh_.valid()) mesh_.Update(std::move(data));
        else               mesh_.Upload(std::move(data));
        view_.dirty = false;
    }

    void uploadTexture(int which) {
        gldx::Texture2DDesc desc = which == 1 ? MakeCheckerDesc()
                             : which == 2 ? MakePlanetDesc()
                                          : MakeUvGridDesc();
        texture_.Upload(desc);
    }

    View& view_;
    std::unique_ptr<gldx::ShaderProgram> program_;       // vertex + fragment
    std::unique_ptr<gldx::ShaderProgram> tessProgram_;   // + TCS / TES, null if unavailable
    float             maxTessLevel_ = 1.0f;
    gldx::Mesh        mesh_;
    gldx::Texture2D   texture_;
    int               loadedTexture_ = -1;   // -1 => not uploaded yet
};

// Live read-out of exactly what each of the two subdivision paths is costing: the
// cage's own buffer sizes and patch count, the triangles the tessellator generates
// on top of them, the flags in force, and the key legend.
class HudPass : public gldx::RenderPass {
public:
    explicit HudPass(View& view) : RenderPass("Hud"), view_(view) {
        if (!sprite_.Init()) std::fprintf(stderr, "textured_sphere: SpriteBatch init failed\n");
        for (const char* c : kFontCandidates) { if (font_.LoadFromFile(c, 48.0f)) break; }
        white_.Upload(MakeSolidDesc());
    }

    void Execute(gldx::RenderFrame& frame) override {
        if (!view_.hud || !font_.loaded() || !white_.valid()) return;
        const std::size_t lvl = static_cast<std::size_t>(view_.tessLevel);
        // A triangle patch with all levels at L is cut into about L*L triangles, so
        // the cage's patch count times that is the workload the GPU took over. Tilde
        // on purpose: with adaptive levels every patch has its own L.
        const std::size_t generated = patches_ * lvl * lvl;
        char cage[128], mode[192], keys[192];
        std::snprintf(cage, sizeof(cage),
                      "cage %d x %d   verts %zu  idx %zu", view_.segments, view_.rings,
                      vertices_, indices_);
        if (view_.tess)
            std::snprintf(mode, sizeof(mode),
                          "tess ON  level %d %s   ~%zu tris from %zu patches   tex %s%s%s",
                          view_.tessLevel, view_.adaptive ? "adaptive" : "uniform",
                          generated, patches_, kTextureNames[view_.texture],
                          view_.wireframe ? "  wire" : "", view_.cull ? "" : "  cull off");
        else
            std::snprintf(mode, sizeof(mode),
                          "tess OFF (CPU mesh)   %zu tris   tex %s%s%s",
                          patches_, kTextureNames[view_.texture],
                          view_.wireframe ? "  wire" : "", view_.cull ? "" : "  cull off");
        std::snprintf(keys, sizeof(keys),
                      "[ ] seg   - + ring   G tess   A adapt   1-9 level   C cull   T tex   W wire   H hud");
        const char* const lines[3] = {cage, mode, keys};

        float width = 0.0f;
        for (const char* l : lines)
            width = std::max(width, gldx::TextRenderer::Measure(font_, l, 16.0f));
        sprite_.Begin(white_, frame.fbWidth, frame.fbHeight);
        sprite_.Draw(white_, 14.0f, 14.0f, width + 24.0f, 92.0f, 0, 0, 1, 1,
                     glm::vec4(0, 0, 0, 0.45f));
        float y = 22.0f;
        for (const char* l : lines) {
            gldx::TextRenderer::Draw(sprite_, font_, l, 26.0f, y, 16.0f,
                                    glm::vec4(0.85f, 0.92f, 1.0f, 1.0f));
            y += 24.0f;
        }
        sprite_.End();
    }

    void setCounts(std::size_t vertices, std::size_t indices, std::size_t patches) noexcept {
        vertices_ = vertices;
        indices_  = indices;
        patches_  = patches;
    }

private:
    static gldx::Texture2DDesc MakeSolidDesc() {
        gldx::Texture2DDesc d;
        d.width = d.height = 1; d.channels = 4; d.srgb = false;
        d.pixels = {255, 255, 255, 255};
        return d;
    }

    View& view_;
    gldx::SpriteBatch sprite_;
    gldx::Font        font_;
    gldx::Texture2D   white_;
    std::size_t       vertices_ = 0;
    std::size_t       indices_  = 0;
    std::size_t       patches_  = 0;
};

int ClampSegments(int v) {
    return std::min(std::max(v, kMinSegments), kMaxSegments);
}
int ClampRings(int v) {
    return std::min(std::max(v, kMinRings), kMaxRings);
}
int ClampTessLevel(int v) {
    return std::min(std::max(v, kMinTessLevel), kMaxTessLevel);
}

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(
        argc, argv, {"wireframe", "hud", "tess", "adaptive", "cull"},
        {"segments", "rings", "texture", "sphere-radius", "yaw", "pitch", "orbit",
         "tess-level", "snapshot", "snap-at"}, "textured_sphere");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    View view;
    view.segments  = ClampSegments(flags.integer("segments", view.segments));
    view.rings     = ClampRings(flags.integer("rings", view.rings));
    view.tessLevel = ClampTessLevel(flags.integer("tess-level", view.tessLevel));
    view.radius    = flags.real("sphere-radius", view.radius, 0.05f, 20.0f);
    view.wireframe = flags.on("wireframe", false);
    view.hud       = flags.on("hud", true);
    view.tess      = flags.on("tess", false);
    view.adaptive  = flags.on("adaptive", false);
    view.cull      = flags.on("cull", true);
    view.yaw   = flags.real("yaw", view.yaw);
    view.pitch = flags.real("pitch", view.pitch);
    view.orbit = flags.real("orbit", view.orbit, 2.5f, 40.0f);
    const std::string texName = flags.string("texture");
    if (!texName.empty()) {
        int found = -1;
        for (int i = 0; i < kTextureCount; ++i) {
            if (texName == kTextureTokens[i]) found = i;
        }
        if (found >= 0) view.texture = found;
        else std::fprintf(stderr, "textured_sphere: unknown texture '%s', using uv\n", texName.c_str());
    }

    const std::string snapPath = flags.string("snapshot");
    const double      snapAt   = flags.number("snap-at", 2.0);

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - textured_sphere (GeometryFactory::Sphere + TCS/TES + Texture2D)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();

    auto sphere = std::make_unique<SpherePass>(view);
    SpherePass* sphereRef = sphere.get();
    if (!sphereRef->ok()) return 1;
    renderer.AddPass(std::move(sphere));
    auto hud = std::make_unique<HudPass>(view);
    HudPass* hudRef = hud.get();
    renderer.AddPass(std::move(hud));

    window.OnKey([&](gldx::win::Window&, gldx::win::Key key,
                     gldx::win::KeyAction action, int) {
        if (action != gldx::win::KeyAction::Press) return;
        switch (key) {
            case gldx::win::Key::LeftBracket:
            case gldx::win::Key::Left:  view.segments = ClampSegments(view.segments - 2);
                                         view.dirty = true; break;
            case gldx::win::Key::RightBracket:
            case gldx::win::Key::Right: view.segments = ClampSegments(view.segments + 2);
                                         view.dirty = true; break;
            case gldx::win::Key::Minus:
            case gldx::win::Key::Down:  view.rings = ClampRings(view.rings - 1);
                                         view.dirty = true; break;
            case gldx::win::Key::Equal:
            case gldx::win::Key::Up:    view.rings = ClampRings(view.rings + 1);
                                         view.dirty = true; break;
            case gldx::win::Key::T:     view.texture = (view.texture + 1) % kTextureCount;
                                         break;
            // G asks for the patch path; the pass decides whether the driver can
            // actually link one, so an unavailable pair degrades to the CPU mesh
            // instead of drawing nothing.
            case gldx::win::Key::G:     if (sphereRef->tessAvailable())
                                            view.tess = !view.tess;
                                         break;
            case gldx::win::Key::A:     view.adaptive = !view.adaptive; break;
            case gldx::win::Key::C:     view.cull = !view.cull; break;
            case gldx::win::Key::Num1:  view.tessLevel = 1; break;
            case gldx::win::Key::Num2:  view.tessLevel = 2; break;
            case gldx::win::Key::Num3:  view.tessLevel = 3; break;
            case gldx::win::Key::Num4:  view.tessLevel = 4; break;
            case gldx::win::Key::Num5:  view.tessLevel = 5; break;
            case gldx::win::Key::Num6:  view.tessLevel = 6; break;
            case gldx::win::Key::Num7:  view.tessLevel = 7; break;
            case gldx::win::Key::Num8:  view.tessLevel = 8; break;
            case gldx::win::Key::Num9:  view.tessLevel = 9; break;
            case gldx::win::Key::W:     view.wireframe = !view.wireframe; break;
            case gldx::win::Key::H:     view.hud = !view.hud; break;
            default: break;
        }
    });
    window.OnCursor([&](gldx::win::Window&, gldx::win::Vec2d pos) {
        if (!view.dragging) return;
        view.yaw   -= static_cast<float>(pos.x - view.lastX) * 0.006f;
        view.pitch  = std::min(std::max(view.pitch + static_cast<float>(pos.y - view.lastY) * 0.006f,
                                        -1.45f), 1.45f);
        view.lastX = pos.x; view.lastY = pos.y;
    });
    window.OnMouseButton([&](gldx::win::Window&, gldx::win::MouseButton button,
                             gldx::win::KeyAction action, int) {
        if (button != gldx::win::MouseButton::Left) return;
        view.dragging = (action == gldx::win::KeyAction::Press);
        const gldx::win::Vec2d c = window.CursorPos();
        view.lastX = c.x; view.lastY = c.y;
    });
    window.OnScroll([&](gldx::win::Window&, double, double dy) {
        view.orbit = std::min(std::max(view.orbit * (dy > 0.0 ? 0.9f : 1.1f), 2.5f), 40.0f);
    });

    bool snapped = false;
    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;
        renderer.Render(f);
        hudRef->setCounts(sphereRef->vertices(), sphereRef->indices(), sphereRef->patches());

        if (!snapPath.empty() && !snapped && info.time >= snapAt) {
            snapped = true;
            if (window.CaptureScreenshot(snapPath))
                std::printf("textured_sphere: captured %s\n", snapPath.c_str());
            else
                std::fprintf(stderr, "textured_sphere: snapshot failed: %s\n", snapPath.c_str());
            window.Close();
        }
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
