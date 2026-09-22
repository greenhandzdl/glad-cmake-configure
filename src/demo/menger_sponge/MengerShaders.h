#ifndef MENGER_SPONGE_SHADERS_H
#define MENGER_SPONGE_SHADERS_H

/**
 * @file MengerShaders.h
 * @brief The three GLSL 4.10 core stages that generate a Menger sponge on the GPU.
 *
 * Nothing here is a mesh. A level-L Menger sponge is exactly 20^L axis-aligned
 * sub-cubes, because the construction removes the 7 cells of the 3x3x3 grid that
 * have two or more coordinates on the middle third (27 - 7 = 20 survivors). That
 * makes the cube list decodable in base 20: the L-th digit of a sub-cube index
 * picks one of the 20 kept cells of the L-th subdivision. So the whole fractal
 * follows from gl_VertexID alone — zero vertex buffers, zero attributes, one
 * DrawArrays(GL_POINTS) — and `uniform int uLevel` is literally the bound of the
 * decode loop, which is what makes the subdivision depth a runtime parameter
 * rather than a rebuild.
 *
 * Stage responsibilities:
 *   1. VERTEX     decodes gl_VertexID -> the sub-cube centre and half-edge.
 *   2. GEOMETRY   expands each point into a 6-face cube (24 vertices, one
 *                 triangle strip per face) and computes the face normal
 *                 analytically. This is where the volume appears. It also does
 *                 the rejecting: frustum, sub-pixel and back-facing faces are all
 *                 dropped here rather than emitted and discarded later, which is
 *                 the only part of this pipeline where the GPU gets a say in how
 *                 much work it does (see the uCull arm below).
 *   3. FRAGMENT   shades it: two directional lights over a normal-keyed tint,
 *                 plus distance fog so the hole channels read as depth.
 *
 * Kept out of main.cpp so the entry point stays a thin wiring file, exactly as
 * shader_stages keeps its five stages in Stages.h. All three stages go through
 * the one selective entry point gldx::ShaderProgram::CreateFromSources.
 *
 * Winding note: Renderer::Init() sets glFrontFace(GL_CCW) + glCullFace(GL_BACK),
 * and a strip is walked in ZIG-ZAG order, not around the outline. GL reorders the
 * first two vertices of every odd-numbered triangle so a strip keeps a consistent
 * facing, which means the loop order BL,BR,TR,TL makes each face's second triangle
 * come out CW and get culled — the face then renders as half a triangle with the
 * cube's own interior showing through the missing half (verified: that is exactly
 * what a single level-0 cube looked like before this was fixed). The CORNER order
 * below is what makes both triangles of all six faces face outward.
 *
 * GLSL const note: a block-scope const has to be initialised by a constant
 * expression, which is not the C++ reading of "don't write to it again". Locals
 * derived from shader inputs or a loop index cannot be const; only the file-scope
 * literal arrays below may be. Getting this wrong fails the geometry-stage compile
 * with a misleading "Initializer not allowed" plus a cascade of undeclared
 * identifiers, because the failed declaration takes everything after it down too.
 */

#include <cstddef>

namespace menger_sponge {

// 1. VERTEX: the generator. One input vertex per sub-cube, derived from
//    gl_VertexID with no buffer bound behind it.
inline constexpr const char* kVertex = R"GLSL(#version 410 core
// The 20 survivors of one 3x3x3 split, in scan order over the 27 cells: a cell
// is removed when two or more of its coordinates sit on the middle third, which
// is precisely the central cross of square tunnels the Menger construction
// drills out. Enumerating instead of table-lookuping keeps the rule visible.
ivec3 KeptCell(int digit) {
    for (int i = 0; i < 27; ++i) {
        ivec3 c = ivec3(i / 9, (i / 3) % 3, i % 3);
        int middle = (c.x == 1 ? 1 : 0) + (c.y == 1 ? 1 : 0) + (c.z == 1 ? 1 : 0);
        if (middle >= 2) continue;         // one of the 7 removed cells
        if (digit == 0) return c;
        --digit;
    }
    return ivec3(1);                       // unreachable while digit < 20
}

uniform int   uLevel;   // subdivision depth -> exactly 20^uLevel sub-cubes
uniform float uSize;    // edge length of the whole sponge, in world units

out vec3  gCenter;      // sub-cube centre, world units
out float gHalf;        // sub-cube half edge, world units
out float vKeep;        // 1 = draw this cube; the queue arm's LOD rule, always 1 here

void main() {
    // The CPU issues exactly 20^uLevel points, so gl_VertexID is always a legal
    // index and every iteration consumes one in-range base-20 digit.
    int   idx  = gl_VertexID;
    vec3  centre = vec3(0.0);
    float span = uSize;                    // parent edge at this level
    for (int l = 0; l < uLevel; ++l) {
        ivec3 c = KeptCell(idx % 20);
        idx /= 20;
        span /= 3.0;                       // span == uSize / 3^(l+1)
        centre += span * vec3(c - ivec3(1));
    }
    gCenter = centre;
    gHalf   = span * 0.5;                  // span == uSize / 3^uLevel after the loop
    vKeep   = 1.0;                         // every decoded cube is terminal: draw it

    // Never left unwritten: the geometry stage recomputes clip space from the
    // centre, but an undefined gl_Position would feed garbage to primitive
    // assembly ahead of the GS.
    gl_Position = vec4(centre, 1.0);
}
)GLSL";

// 2. GEOMETRY: point -> cube. Six faces of four vertices each, so max_vertices
//    is 24 and every face ends its own strip.
inline constexpr const char* kGeometry = R"GLSL(#version 410 core
layout(points) in;
layout(triangle_strip, max_vertices = 24) out;

in vec3  gCenter[];
in float gHalf[];
in float vKeep[];   // 0 = this cube is about to be subdivided, so drawing it would bury
                    // its own children under a solid block (see kQueueVertex)

out vec3 vNormal;
out vec3 vWorld;

uniform mat4  uViewProj;
uniform vec3  uEye;
// Both derived from the same projection element, so the CPU hands over the two
// products the tests below need instead of the matrix parts:
//   uPxScale = 0.5 * fbHeight * proj[1][1]  -> world radius r at depth w covers
//                                              2 * r * uPxScale / w pixels
//   uClipPad = proj[1][1] * max(1, fbHeight / fbWidth)
//                                          -> a world radius r spans at most
//                                             r * uClipPad in clip space on *both*
//                                             axes, which is what makes padding the
//                                             x and y tests with it conservative.
uniform float uPxScale;
uniform float uClipPad;
uniform int   uCull;   // 0 = emit everything, for the control arm of the measurement

const vec3 AXIS[3] = vec3[3](vec3(1.0, 0.0, 0.0),
                             vec3(0.0, 1.0, 0.0),
                             vec3(0.0, 0.0, 1.0));
// Zig-zag, not around: (bottom-left, bottom-right, top-left, top-right). With the
// strip's own vertex reordering this makes both emitted triangles of a face wind
// CCW as seen from that face's outward side, for either sign of the normal (the
// negative faces swap the in-plane axes to keep the handedness).
const vec2 CORNER[4] = vec2[4](vec2(-1.0, -1.0), vec2(1.0, -1.0),
                               vec2(-1.0,  1.0), vec2(1.0,  1.0));

void main() {
    // None of these may be const: GLSL wants a constant-expression initializer for
    // block-scope consts, and all of these come from inputs or a loop index.
    vec3  c = gCenter[0];
    float h = gHalf[0];

    // Cheapest possible test, so it goes ahead of every other rejection.
    if (vKeep[0] < 0.5) return;

    vec4 clip = uViewProj * vec4(c, 1.0);

    // Three rejections, all of them before a single vertex is emitted. Each is
    // deliberately conservative: the point of a GPU-side cull is that it must not
    // change the picture, so it may keep too much but never drop a cube that
    // covers a pixel. Measured as a pixel diff against --off cull, the expected
    // result is *zero* difference; frame time is where the win shows up.
    if (uCull == 1) {
        // Half-diagonal, not half-edge, so a cube straddling a frustum plane is
        // never dropped whole.
        float r = h * 1.7320508;
        // w <= 0 is the near plane and everything behind the camera; the far plane
        // is not tested because the cube's clip-space z extent scales with a
        // different matrix element and getting that bound wrong is the one way to
        // make this exact test inexact. Nothing here is beyond it anyway.
        if (clip.w <= 0.0) return;
        float pad = r * uClipPad;
        if (abs(clip.x) > clip.w + pad) return;
        if (abs(clip.y) > clip.w + pad) return;
        // Sub-pixel: the whole cube is under one pixel, so its 24 vertices can
        // never resolve into anything. This is the rejection that moves the level
        // ceiling, because it turns the emitted-vertex count from a function of
        // 20^level into a function of screen area.
        if (2.0 * r * uPxScale < clip.w) return;
    }

    vec3 view = c - uEye;

    for (int a = 0; a < 3; ++a) {
        vec3 n = AXIS[a];
        vec3 t = AXIS[(a + 1) % 3];
        vec3 b = AXIS[(a + 2) % 3];
        for (int s = 0; s < 2; ++s) {
            bool pos = (s == 0);
            vec3 sgn = pos ? n : -n;                  // outward normal
            vec3 u   = pos ? t : b;                   // in-plane basis...
            vec3 v   = pos ? b : t;                   // ...swapped to keep CCW
            // A cube is convex, so dot(N, P - eye) >= 0 is an exact back-face test,
            // not the screen-space-winding approximation a rasterizer uses. Half of
            // every cube's faces fail it, and skipping them here is what keeps those
            // 12 vertices from being emitted, transformed and then thrown away.
            if (uCull == 1 && dot(sgn, view) >= 0.0) continue;
            for (int i = 0; i < 4; ++i) {
                vec3 p = c + h * (sgn + u * CORNER[i].x + v * CORNER[i].y);
                vNormal     = sgn;
                vWorld      = p;
                gl_Position = uViewProj * vec4(p, 1.0);
                EmitVertex();
            }
            EndPrimitive();
        }
    }
}
)GLSL";

// 3. FRAGMENT: shading only, no generation. Kept cheap on purpose — the subject
//    of this demo is where the geometry comes from, not the BRDF.
inline constexpr const char* kFragment = R"GLSL(#version 410 core
in vec3 vNormal;
in vec3 vWorld;
out vec4 FragColor;

uniform vec3  uEye;
uniform float uFog;   // density; 0 turns the fog off

// The background the fog fades into. Must match the pass' glClear colour (see
// kBgColor in main.cpp); the no-post path outputs colour as-is, so both are
// already display-referred.
const vec3 kBgColor = vec3(0.043, 0.051, 0.071);

// One tint per axis pair, so the three orthogonal tunnel systems of the sponge
// are distinguishable at a glance instead of reading as one grey blob.
const vec3 kTintX = vec3(0.86, 0.72, 0.44);
const vec3 kTintY = vec3(0.55, 0.88, 0.66);
const vec3 kTintZ = vec3(0.48, 0.70, 0.98);

void main() {
    vec3 n    = normalize(vNormal);
    vec3 key  = normalize(vec3( 0.60, 0.80, 0.40));
    vec3 fill = normalize(vec3(-0.40, 0.15, -0.90));

    // n*n is 1 on the dominant axis and 0 on the other two, which is exactly the
    // per-face-channel selector wanted above.
    vec3 w    = n * n;
    vec3 tint = w.x * kTintX + w.y * kTintY + w.z * kTintZ;

    vec3 col = tint * (0.20 + 0.80 * max(dot(n, key), 0.0))
             + tint * (0.22 * max(dot(n, fill), 0.0));

    float d = distance(uEye, vWorld);
    col = mix(col, kBgColor, 1.0 - exp(-uFog * d));
    FragColor = vec4(col, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// The GPU-driven subdivision queue, stages 4-6. They replace "the CPU enumerates
// 20^level points and the GPU builds all of them" with "the CPU hands over one cube
// and the GPU decides what to subdivide".
//
// One pass over the queue does two things with the same buffer of cubes and the same
// count, because a cube is either finished or still worth splitting:
//
//   1. DRAW the finished ones, right now, through the cube-expanding geometry stage of
//      the exhaustive arm (stage 2 above) - which is why that stage gained vKeep.
//   2. CAPTURE the children of the ones that are not finished, into the next buffer,
//      through stage 5. A finished cube writes nothing here at all.
//
// The split matters more than it looks: it is what keeps the queue buffer holding only
// *live* work. Forwarding finished cubes into the buffer as leaves instead - the obvious
// shape of this pipeline - makes the buffer have to hold every cube the frame ends up
// drawing, and since a subdivider writes 20 cubes per invocation, an index-only budget
// has to reserve 20 slots per invocation, which caps the whole frame at capacity/20
// cubes. Measured that way, level 6 drew *fewer* cubes than level 5, because the leaf set
// had outgrown the reservation. Drawing the leaves instead of storing them removes the
// cap from the thing that grows and leaves it on the thing that does not.
//
// After the last pass, one more draw with uRemaining = 0 renders whatever is still in
// the queue, so the level-of-detail sponge is the union of all L+1 draws and its total
// cube count is a number no CPU-side code ever held.
//
// The consequence worth stating plainly: this renders a different object than the
// exhaustive arm. A cube that stops being subdivided is drawn solid, so its interior
// structure is approximated rather than exact. That is the trade that removes the
// exponential wall, and it is why --off subdivide produces a non-zero pixel diff
// while --off cull must not produce one.
//
// Three invariants the C++ side depends on:
//   - vKeep has exactly one home (stage 4), and it includes the queue budget. Both
//     branches read it, so a cube is drawn or split, never both and never neither - which
//     is what a duplicated LOD test would eventually get wrong, in the form of a solid
//     block sitting on top of its own children or a hole where a truncated subtree used
//     to be. It is also why the budget cannot live in the capture stage: refusing a write
//     there without re-marking the cube as finished is precisely how you produce the hole.
//   - every cube in the buffer already carries its own half edge in .w, so no draw needs
//     a level uniform: it inflates what it is handed. That is what lets the queue arm
//     share the exhaustive arm's geometry and fragment stages verbatim.
//   - the two halves of a pass are the same vertex stage over the same buffer with the
//     same uniforms; they differ only in which one rasterizes and which one captures.
//
// One 4.1 constraint worth knowing before trying to simplify this: the capture target
// cannot be declared in the shader. layout(xfb_buffer) / layout(xfb_stride) output
// qualifiers arrived with GLSL 4.30, and a #version 410 core shader rejects them with
// "Unknown identifier 'xfb_buffer' in layout" (verified on the Apple 4.1 Metal
// driver). Hence gldx::TransformFeedbackDesc on the C++ side.
// ---------------------------------------------------------------------------

// 4. QUEUE VERTEX: read one cube out of the buffer the previous pass wrote and answer
//    the only question the queue has. Nothing is generated here; the outputs exist so
//    that two different geometry stages can branch on the same verdict.
inline constexpr const char* kQueueVertex = R"GLSL(#version 410 core
layout(location = 0) in vec4 aCube;   // xyz = centre, w = half edge

out vec4  vCube;      // for the capture stage, which writes children out of this
out vec3  gCenter;    // for the cube-expanding stage, which is the exhaustive arm's
out float gHalf;
out float vKeep;     // 1 = this cube is finished, draw it; 0 = split it

uniform mat4  uViewProj;
uniform float uPxScale;    // same quantity the cube stage uses, see its declaration
uniform int   uRemaining;  // subdivisions still allowed below a cube at this depth
uniform float uMinPx;      // projected edge above which subdividing buys anything
uniform int   uBudget;     // how many cubes of this pass may split at all

void main() {
    vCube   = aCube;
    gCenter = aCube.xyz;
    gHalf   = aCube.w;
    gl_Position = vec4(aCube.xyz, 1.0);

    // The projected edge of the cube in pixels, in the same units the sub-pixel rejection
    // in the cube stage uses.
    float w = (uViewProj * vec4(aCube.xyz, 1.0)).w;
    // A cube at or behind the eye is finished rather than split: it cannot resolve detail
    // anyway, and feeding it children would grow the queue with work that never ends.
    float px = w > 1e-5 ? 2.0 * aCube.w * uPxScale / w : 0.0;

    // The queue ceiling, tested here rather than in the capture stage on purpose. A cube
    // that cannot split still has to be *drawn*, and the only way the draw half learns
    // that is if the same verdict reaches it: the capture stage's own index test would
    // have silently dropped the whole subtree, leaving a hole where a coarser cube should
    // have stood. So an over-budget frame degrades into fewer, larger cubes instead of
    // missing geometry.
    //
    // gl_VertexID is the invocation index for a GL_POINTS draw with one vertex per
    // primitive, which is the only shape these buffers are ever drawn in, so it agrees
    // with the primitive count the reservation is phrased in.
    //
    // The surviving bias: the cut is by queue order, and queue order follows the base-20
    // digit order, whose most significant digit picks the coarsest cell. Past the budget
    // it is therefore one corner of the sponge that stays coarse, not an even scatter.
    // Fixing that means splitting the largest cubes first, which needs a sort - and 4.1
    // offers no compute stage, no SSBO and no atomic counter to build one with.
    vKeep = (uRemaining <= 0 || px < uMinPx || gl_VertexID >= uBudget) ? 1.0 : 0.0;
}
)GLSL";

// 5. QUEUE GEOMETRY: the fan-out. One EndPrimitive per child, so the captured primitive
//    count is exactly the number of cubes the next pass has to work on. Nothing is
//    decided here: every cube reaching it has already been declared worth splitting by
//    the vertex stage, budget included, so this stage is a pure 1-into-20 expansion.
inline constexpr const char* kQueueGeometry = R"GLSL(#version 410 core
layout(points) in;
layout(points, max_vertices = 20) out;

in vec4  vCube[];
in float vKeep[];
out vec4 gOut;      // the captured varying: named in the C++ TransformFeedbackDesc

void main() {
    // Finished cubes are being drawn this same pass off this same buffer, so they write
    // nothing here. This is also why the buffer holds only live work: a pass whose cubes
    // are all finished writes an empty queue and the next one draws nothing.
    if (vKeep[0] > 0.5) return;

    // The same 3x3x3 rule the index decoder uses, walked directly instead of indexed:
    // enumerate all 27 cells and skip the 7 with two or more coordinates on the middle
    // third. Emitting from the loop keeps one visible source for the rule rather than a
    // table that has to agree with it.
    //
    // The child half edge is h/3 but the *offset* is 2h/3 per grid step, because the
    // three slices divide the parent's full edge 2h, not its half edge. Writing the same
    // value for both is the easy mistake and it does not look wrong in a wireframe: the
    // children stay inside the parent and keep the right relative size, they just cluster
    // towards the middle and leave a gap at the parent's own skin.
    float child = vCube[0].w / 3.0;
    for (int i = 0; i < 27; ++i) {
        ivec3 cell = ivec3(i / 9, (i / 3) % 3, i % 3);
        int middle = (cell.x == 1 ? 1 : 0) + (cell.y == 1 ? 1 : 0) + (cell.z == 1 ? 1 : 0);
        if (middle >= 2) continue;
        gOut = vec4(vCube[0].xyz + 2.0 * child * vec3(cell - ivec3(1)), child);
        EmitVertex();
        EndPrimitive();
    }
}
)GLSL";

// 6. QUEUE FRAGMENT: exists because a 4.1 graphics program cannot link without a
//    fragment stage. GL_RASTERIZER_DISCARD is on for the only draw that uses this
//    program, so it never actually runs.
inline constexpr const char* kQueueFragment = R"GLSL(#version 410 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0); }
)GLSL";

} // namespace menger_sponge

#endif // MENGER_SPONGE_SHADERS_H
