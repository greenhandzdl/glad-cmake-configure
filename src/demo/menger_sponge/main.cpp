/**
 * @file main.cpp
 * @brief menger_sponge - a fractal that exists only because shaders can build
 *        geometry: the subdivision level is a uniform, not a mesh rebuild.
 *
 * A level-L Menger sponge is exactly 20^L axis-aligned sub-cubes (the 3x3x3 split
 * keeps the 20 cells that are not part of the central cross of holes), so the cube
 * list is a base-20 number and every digit is one subdivision step. That makes it
 * decodable straight from gl_VertexID: the vertex stage turns an index into a cube
 * centre, the geometry stage inflates each centre into a 24-vertex cube, and the
 * only thing the CPU uploads per frame is the loop bound. There is no vertex
 * buffer, no attribute, no Mesh and no Scene here — the geometry never exists on
 * the host at any point, which is the entire point of the demo.
 *
 * `uniform int uLevel` therefore *is* the subdivision: raising it does not rebuild
 * anything, it just runs the decode loop once more and multiplies the point count
 * by 20. --level / Up / Down / +/- change it at runtime.
 *
 * Cost is exponential and that is the hard limit, not the shader: 20^L cubes times
 * 24 geometry-stage vertices is 192k at L=3 and 3.84M at L=4. The geometry stage
 * therefore also does the rejecting (see uCull in MengerShaders.h): frustum-outside,
 * sub-pixel and back-facing faces are dropped before a vertex is emitted, which is
 * what buys L=5 - 3.2M cubes to walk, but only the ones that cover a pixel to build.
 * L=0 is the plain cube the recursion starts from, which makes a useful baseline for
 * comparing frames.
 *
 * What the cull does *not* change is the enumeration: the CPU still issues 20^level
 * points, and the geometry stage has to transform each centre once just to find out
 * it is not worth drawing. Measured on an Apple GPU at the 1600x1200 framebuffer (the
 * default 800x600 window at 2x content scale), vsynced, --off spin,hud: level 4 goes
 * 28.4ms -> 19.0ms mean and level 5 468ms -> 317ms, so the cull buys 1.5x and level 5
 * stays unusable at ~3 fps. The 1.5x is emission; the remaining 317ms is the walk, and
 * no amount of rejecting can remove a vertex shader invocation that already happened.
 *
 * That wall is why the demo has a second arm, and it is the default one. Instead of
 * enumerating, it lets the GPU decide: the CPU uploads *one* cube, and a chain of
 * transform-feedback passes reads a buffer of cubes, draws the ones already projected
 * smaller than --minpx pixels straight away, and writes the 20 children of the rest
 * back for the next pass to consume. Every one of those draws takes its vertex count
 * from whatever the pass before it recorded - a number the CPU never holds, never
 * predicts and never reads back on the render path. The exponential is gone because
 * the walk is bounded by screen area rather than by 20^level, which is what buys the
 * levels the exhaustive arm cannot reach at all.
 *
 * The price is that the two arms do not draw the same object. A cube that stops being
 * subdivided is drawn solid, so the default arm renders a level-of-detail sponge: exact
 * near the camera, approximated in the distance. The queue is also finite, so a frame can
 * ask for more splits than it holds room for; past that budget the remaining cubes are
 * simply treated as finished and drawn coarser, never dropped, which is what keeps a
 * saturated level from sprouting holes. --off subdivide switches back to the exact
 * enumeration for comparison, and unlike --off cull the expected pixel diff here is
 * *non-zero*, because this switch changes coverage rather than only work.
 *
 * Wiring follows the repo's conventions: every stage goes through the one selective
 * entry point gldx::ShaderProgram::CreateFromSources (the queue program additionally
 * declares its captured varying through the TransformFeedbackDesc overload), the draws
 * go through a gldx::RenderPass owned by a gldx::Renderer, the capture sessions are
 * gldx::TransformFeedback objects, the camera is gldx::Camera, the switches are
 * gldx::cli::Flags and the whole lifecycle is gldx::win's - no direct GLFW calls
 * beyond the frame-level GL state this pass owns.
 *
 * Controls: Up/Down or +/- subdivision, Space toggles the orbit, H toggles the HUD,
 * C toggles the geometry-stage cull, G toggles the subdivision arm, Esc closes.
 *
 * --level N     subdivision depth (default 3; saturates at 8 subdivided, 5 exhaustive)
 * --size F      sponge edge length in world units (default 2)
 * --distance F  camera orbit radius (default: framed from --size)
 * --minpx F     projected edge below which a cube stops being subdivided (default 6)
 * --off subdivide,cull,hud,spin
 * --quit-after SECONDS  headless auto-close
 *
 * --off cull is the control arm for measuring the cull, and the expected result of
 * comparing it against the default is a *zero* pixel diff: a correct cull only ever
 * removes work, never coverage. Frame time is where it shows up. --off subdivide is
 * the control arm for the queue, and it is also the only way to ask for the exact 20^L
 * cube set; its level ceiling drops to 5, because past that the enumeration itself is
 * the cost. Three caveats for scripted runs: the HUD carries a live fps readout, so it
 * is a function of frame timing rather than of the clock and no two HUD frames are
 * ever pixel-identical (compare with --off hud); --off cull --off subdivide --level 5
 * asks for 76.8M geometry-stage vertices, which is slow enough to be mistaken for a
 * hang; and the subdivided arm's cube count is a function of the camera, so a frame is
 * still a pure function of the clock only because the orbit is deterministic.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "MengerShaders.h"

namespace {

// 20^level: the sub-cube count of the *exhaustive* arm, which is simultaneously the
// vertex count of its single DrawArrays(GL_POINTS) call. The vertex shader consumes the
// same number of base-20 digits, so this function and uLevel must stay in lockstep.
// 64-bit because the ceiling of the subdivided arm is past what 20^L fits in: 20^8 is
// 2.6e10, and the only reason the queue never notices is that it never enumerates.
constexpr std::int64_t CubeCount(int level) {
    std::int64_t n = 1;
    for (int i = 0; i < level; ++i) n *= 20;
    return n;
}

// Each cube costs 24 geometry-stage vertices (6 faces x a 4-vertex strip).
constexpr int kVertsPerCube = 24;

// Subdivision depth bounds, per arm.
//
// The exhaustive ceiling is where 20^L stops being affordable to walk: L=5 is 3.2M
// points and measures ~3 fps already, so L=6 would be 64M points and minutes per frame.
// It is a wall-clock bound, not a type or shader limit (20^6 still fits a GLsizei).
//
// The subdivided ceiling is arbitrary in comparison, because that arm's cost is set by
// screen area and --minpx rather than by the level: measured at the 1600x1200 framebuffer
// with the default --minpx 6, the queue has drained by pass 5, so levels 6, 7 and 8
// capture zero cubes, draw zero, and render exactly what level 5 does. Held above that
// wall on purpose - it leaves room to watch the queue stop early on its own, and --minpx
// moves the wall rather than the ceiling.
constexpr int kMinLevel = 0;
constexpr int kMaxLevelExact     = 5;
constexpr int kMaxLevelSubdivided = 8;

constexpr int ClampLevel(int level, bool subdivide) {
    const int max = subdivide ? kMaxLevelSubdivided : kMaxLevelExact;
    if (level < kMinLevel) return kMinLevel;
    if (level > max)       return max;
    return level;
}

// How many cubes one queue buffer can hold, and therefore how much *live* work a pass
// may hand to the next one. Two of these are allocated and ping-ponged: 512k vec4s is
// 8 MB each, which is the price of never telling the CPU how big the workload is.
// Sized for the frontier rather than for the picture, because finished cubes get drawn
// on the spot instead of being stored: measured at the 1600x1200 framebuffer with
// --minpx 6, the queue peaks at 291240 cubes while the frame draws 436678 of them.
constexpr GLsizei kQueueCapacity = 1 << 19;

// Invocations a single queue pass may write, derived from the capacity rather than set
// beside it: each may emit up to 20 children, so the product is the byte-count invariant
// the geometry stage's gl_PrimitiveIDIn test defends. Only cubes still worth splitting
// are charged against it, so the quantity that has to fit here is the queue, not the
// cube set the frame renders. Going over it is a silent loss of detail, so the HUD
// reports the peak queue size beside this number.
constexpr GLsizei kQueueBudget = kQueueCapacity / 20;

// Has to stay in step with kBgColor in the fragment stage: the fog fades into the
// colour the frame was cleared to, so the two are one constant split across files.
constexpr float kBgColor[3] = {0.043f, 0.051f, 0.071f};

// A fixed 3/4 view so a --off spin frame depends only on the wall clock, which is
// what makes level-to-level screenshot comparison a valid measurement.
constexpr float kBaseYaw  = 0.6154f;   // ~35.3 degrees: the classic isometric-ish angle
constexpr float kPitchUp  = 0.55f;     // camera height as a fraction of the orbit radius
constexpr float kSpinRate = 0.35f;     // radians per second
constexpr float kFog      = 0.12f;     // density; scales with the auto camera distance

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width    = d.height = 1;
    d.channels = 4;
    d.srgb     = false;
    d.pixels   = {255, 255, 255, 255};
    return d;
}

/**
 * The one varying the queue program captures: a vec4 per cube, written by its geometry
 * stage into binding point 0. It has to be listed here rather than qualified in the
 * shader, because layout(xfb_buffer) is GLSL 4.30 and this project is pinned to 4.10
 * core (see gldx::TransformFeedbackDesc).
 */
constexpr const char* const kQueueVaryings[] = { "gOut" };

class MengerPass : public gldx::RenderPass {
public:
    MengerPass() : RenderPass("Menger") {}

    // Render-thread only, called from Window::OnCreate with the context current.
    bool Create(int level, float size, bool hud, bool cull, bool subdivide, float minPx) {
        size_      = size;
        hud_       = hud;
        cull_      = cull;
        subdivide_ = subdivide;
        minPx_     = minPx;
        level_     = ClampLevel(level, subdivide_);

        // Arm 1, exhaustive: gl_VertexID -> base-20 digits -> cube. The original shape
        // of the demo, kept as the control arm because it is the only one that renders
        // the exact 20^level cube set rather than a level-of-detail approximation.
        auto exact = gldx::ShaderProgram::CreateFromSources({
            {gldx::ShaderStage::Vertex,   menger_sponge::kVertex},
            {gldx::ShaderStage::Geometry, menger_sponge::kGeometry},
            {gldx::ShaderStage::Fragment, menger_sponge::kFragment},
        });
        // Arm 2's queue: one cube in, twenty children out for the cubes still worth
        // splitting and nothing at all for the ones that are finished. The fragment stage
        // exists only so a 4.1 graphics program can link; the one draw that uses it runs
        // under GL_RASTERIZER_DISCARD, so it is never reached.
        auto queue = gldx::ShaderProgram::CreateFromSources({
            {gldx::ShaderStage::Vertex,   menger_sponge::kQueueVertex},
            {gldx::ShaderStage::Geometry, menger_sponge::kQueueGeometry},
            {gldx::ShaderStage::Fragment, menger_sponge::kQueueFragment},
        }, gldx::TransformFeedbackDesc{ std::span<const char* const>(kQueueVaryings) });
        // Arm 2's other half: the finished cubes of the same buffer, through the *same*
        // cube-expanding geometry and shading stages the exhaustive arm uses. Both arm-2
        // programs share a vertex stage, which is where the single LOD verdict lives.
        auto draw = gldx::ShaderProgram::CreateFromSources({
            {gldx::ShaderStage::Vertex,   menger_sponge::kQueueVertex},
            {gldx::ShaderStage::Geometry, menger_sponge::kGeometry},
            {gldx::ShaderStage::Fragment, menger_sponge::kFragment},
        });
        if (!exact || !queue || !draw) {
            // One log is enough: all three are assembled from the same header, so a
            // second failure is almost always the same typo downstream of the first.
            const bool badQueue = !queue;
            const auto& src = !exact ? exact : (badQueue ? queue : draw);
            std::fprintf(stderr, "menger_sponge: %s program failed to build: %s\n",
                         !exact ? "exact" : (badQueue ? "queue (transform feedback)"
                                                      : "queue draw"),
                         src.error().c_str());
            return false;
        }
        exact_ = std::move(*exact);
        queue_ = std::move(*queue);
        draw_  = std::move(*draw);

        // Two VAOs because the arms have opposite input stories: the exhaustive one
        // needs *a* VAO bound for the draw to be legal but no attributes at all, while
        // the buffer-fed ones need attribute 0 and nothing else.
        idVao_.Create();
        cubeVao_.Create();

        // The root cube: one vec4, which is the entire workload the CPU uploads for the
        // queue arm. It keeps a buffer of its own because it is the one cube list that
        // must never be a capture destination, and pass 0 would otherwise read and write
        // the same storage.
        const glm::vec4 root(0.0f, 0.0f, 0.0f, size_ * 0.5f);
        seed_.Create(GL_ARRAY_BUFFER, std::span<const glm::vec4>{ &root, 1 });
        for (auto& buf : ping_) {
            buf.Reserve(GL_ARRAY_BUFFER,
                        static_cast<std::size_t>(kQueueCapacity) * sizeof(glm::vec4),
                        GL_DYNAMIC_DRAW);
        }

        // Pass p always captures into ping_[p % 2], so the bindings are set up once here
        // rather than per frame, and no pass ever reads the buffer it writes. One
        // transform feedback object per pass is what lets pass p+1 draw the count pass p
        // recorded while keeping its own count alongside it; sharing one object would
        // work only if glDrawTransformFeedback were guaranteed to read the count before
        // the next Begin() resets it, which is not worth depending on.
        for (std::size_t p = 0; p < tf_.size(); ++p) {
            tf_[p].Create();
            tf_[p].Bind();
            tf_[p].AttachBuffer(0, ping_[p % 2]);
            tf_[p].Unbind();
        }

        if (!sprite_.Init()) std::fprintf(stderr, "menger_sponge: SpriteBatch init failed\n");
        for (const char* candidate : kFontCandidates) {
            if (font_.LoadFromFile(candidate, 48.0f)) break;
        }
        white_.Upload(MakeSolidDesc());

        std::printf("menger_sponge: %s arm, cull %s, %d bytes of vertex data uploaded\n",
                    subdivide_ ? "GPU-driven subdivision" : "exhaustive enumeration",
                    cull_ ? "on" : "off",
                    subdivide_ ? static_cast<int>(sizeof(glm::vec4)) : 0);
        DescribeLevel();
        return true;
    }

    void Execute(gldx::RenderFrame& f) override {
        // SpriteBatch::Begin switches depth and culling off and does not restore
        // them, and a pass owns the state its own draw needs, so re-assert it here
        // rather than trusting what Renderer::Init set two frames ago.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(kBgColor[0], kBgColor[1], kBgColor[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (f.camera) {
            if (subdivide_) RunQueue(f);
            else            RunExact(f);
        }

        if (hud_) DrawHud(f);
    }

    // ---- control surface (main's key handler forwards here) ----
    void setLevel(int level) {
        const int clamped = ClampLevel(level, subdivide_);
        if (clamped == level_) return;
        level_ = clamped;
        DescribeLevel();
    }
    [[nodiscard]] int level() const noexcept { return level_; }
    void addLevel(int delta) { setLevel(level_ + delta); }
    void setHud(bool on) noexcept { hud_ = on; }
    [[nodiscard]] bool hud() const noexcept { return hud_; }
    void setCull(bool on) noexcept { cull_ = on; }
    [[nodiscard]] bool cull() const noexcept { return cull_; }
    void setSubdivide(bool on) {
        if (on == subdivide_) return;
        subdivide_ = on;
        // The two arms have different ceilings for different reasons, so switching arms
        // can clamp on its own: 6..8 is unreachable by enumeration, and 6 by walking
        // 64M points is not a thing to leave running interactively.
        const int clamped = ClampLevel(level_, subdivide_);
        const bool moved  = clamped != level_;
        level_ = clamped;
        std::printf("menger_sponge: %s arm (%s)%s\n", on ? "GPU-driven" : "exhaustive",
                    on ? "subdivides until a cube is under --minpx pixels"
                       : "enumerates every one of 20^level cubes",
                    moved ? ", level clamped to this arm's ceiling" : "");
        DescribeLevel();
    }
    [[nodiscard]] bool subdivide() const noexcept { return subdivide_; }
    // The largest queue any pass had to work through, i.e. how close the frame came to
    // the budget that starts coarsening detail, and the queue the closing draw rendered.
    // Both one frame stale by design (see Harvest), which is fine for a print and is the
    // reason neither of them can slow a frame down.
    [[nodiscard]] std::int64_t peakQueue() const noexcept { return peakQueue_; }
    [[nodiscard]] std::int64_t frontierCubes() const noexcept { return frontier_; }

private:
    // One line for two different questions. "How many cubes is this" is an exact number
    // in one arm and an unknown in the other, and that difference is the point of the
    // demo, so it is printed as one.
    void DescribeLevel() const {
        if (subdivide_) {
            std::printf("menger_sponge: uLevel -> %d (%d transform-feedback passes, "
                        "cube count decided by the GPU, queue holds up to %lld live cubes)\n",
                        level_, level_, static_cast<long long>(kQueueBudget));
        } else {
            const std::int64_t cubes = CubeCount(level_);
            std::printf("menger_sponge: uLevel -> %d (%lld sub-cubes, %lld geometry-stage "
                        "vertices enumerated by the CPU)\n", level_,
                        static_cast<long long>(cubes),
                        static_cast<long long>(cubes * kVertsPerCube));
        }
    }

    // 0.5 * fbHeight * proj[1][1]: a world radius r at clip depth w covers
    // 2 * r * this many pixels. Both arms need it, one for the LOD decision and one for
    // the sub-pixel rejection.
    static float PxScale(const gldx::RenderFrame& f) {
        const float fh = static_cast<float>(f.fbHeight > 0 ? f.fbHeight : 1);
        return 0.5f * fh * f.viewProj[1][1];
    }

    // proj[1][1] * max(1, h/w): the clip-space reach of a world radius on *both* axes,
    // which is what lets the geometry stage pad the x and y tests with one value.
    static float ClipPad(const gldx::RenderFrame& f) {
        const float fh = static_cast<float>(f.fbHeight > 0 ? f.fbHeight : 1);
        const float fw = static_cast<float>(f.fbWidth  > 0 ? f.fbWidth  : 1);
        return f.viewProj[1][1] * std::max(1.0f, fh / fw);
    }

    float Fog() const noexcept { return kFog * (2.0f / size_); }   // fog tracks framing

    // Point the buffer-fed VAO's single attribute at `src`. Stride 0 means "tightly
    // packed", which is right for one vec4 per vertex. The caller binds the VAO: the
    // pointer is captured against the *current* VAO and the current GL_ARRAY_BUFFER, so
    // both have to be in place at the same time.
    void Repoint(const gldx::GLBuffer& src) const {
        src.Bind(GL_ARRAY_BUFFER);
        cubeVao_.AttachAttribute(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
        src.Unbind(GL_ARRAY_BUFFER);
    }

    // The camera and the two scalars every cube-expanding draw needs. Shared by the
    // exhaustive arm and by the queue's draw program because both run the same geometry
    // stage - the two arms differ in where the cubes come from, not in what happens to
    // them after.
    void SetViewUniforms(const gldx::ShaderProgram& prog, const gldx::RenderFrame& f) const {
        prog.Set("uViewProj", f.viewProj);
        prog.Set("uEye", f.camera->Position());
        prog.Set("uPxScale", PxScale(f));
        prog.Set("uClipPad", ClipPad(f));
        prog.Set("uCull", cull_ ? 1 : 0);
    }

    // The one place a queue draw gets its vertex count from: `p` captures have happened,
    // so the first draw consumes the single root cube - the only CPU-known count in this
    // arm - and every later one consumes a count the GPU wrote.
    void DrawFrontier(int p) const {
        if (p == 0) cubeVao_.DrawArrays(GL_POINTS, 0, 1);
        else        cubeVao_.DrawTransformFeedback(GL_POINTS, tf_[static_cast<std::size_t>(p - 1)]);
    }

    // The exhaustive arm: one draw call whose vertex count is 20^level.
    void RunExact(const gldx::RenderFrame& f) {
        if (!exact_.valid()) return;
        exact_.Use();
        exact_.Set("uLevel", level_);                       // == the loop bound
        exact_.Set("uSize", size_);
        exact_.Set("uFog", Fog());
        SetViewUniforms(exact_, f);

        idVao_.Bind();
        idVao_.DrawArrays(GL_POINTS, 0, static_cast<GLsizei>(CubeCount(level_)));
        idVao_.Unbind();
    }

    // The GPU-driven arm: level_ passes, each of which draws the finished cubes and
    // captures the children of the rest, then one last draw of whatever frontier is left.
    // Nothing here looks at a cube count, and the only readback is gated on availability
    // so it cannot stall the frame.
    void RunQueue(const gldx::RenderFrame& f) {
        if (!queue_.valid() || !draw_.valid()) return;

        // Last frame's queue sizes, read before this frame's captures overwrite them.
        Harvest();

        // Both arm-2 programs run the same vertex stage, so both need the full set of
        // uniforms that verdict is phrased in - including uBudget, which must carry the
        // same value in both: it decides who splits, and the draw half has to agree with
        // the capture half about that or an over-budget cube would neither split nor draw.
        // Everything else differs because only the draw rasterizes, so only the draw
        // needs the shading and rejection set.
        queue_.Set("uViewProj", f.viewProj);
        queue_.Set("uPxScale", PxScale(f));
        queue_.Set("uMinPx", minPx_);
        queue_.Set("uBudget", static_cast<int>(kQueueBudget));
        draw_.Set("uMinPx", minPx_);
        draw_.Set("uBudget", static_cast<int>(kQueueBudget));
        SetViewUniforms(draw_, f);
        draw_.Set("uFog", Fog());

        // Rasterizer discard is global state, and here the two kinds of queue draw
        // alternate, so it goes on around each capture rather than around the whole loop.
        // Save and restore it the way ParticleBatch does rather than assume the next pass
        // in the frame starts from a known value.
        GLboolean hadDiscard = GL_FALSE;
        glGetBooleanv(GL_RASTERIZER_DISCARD, &hadDiscard);

        cubeVao_.Bind();
        for (int p = 0; p < level_; ++p) {
            // Pass p works on depth-p cubes, so level_-p splits are still allowed below
            // them. One value drives both halves of the pass because both halves ask the
            // same vertex stage - which is the only way they can be trusted to answer the
            // same question.
            const int remaining = level_ - p;
            queue_.Set("uRemaining", remaining);
            draw_.Set("uRemaining", remaining);

            Repoint(p == 0 ? seed_ : ping_[static_cast<std::size_t>(p - 1) % 2]);

            // 1. The finished cubes, drawn now instead of stored. This is what keeps the
            //    queue buffer holding only live work, and therefore what keeps the buffer
            //    - and the 20-slot reservation every live cube has to make in it - from
            //    having to hold every cube the frame ends up rendering.
            draw_.Use();
            DrawFrontier(p);

            // 2. The children of everything still worth splitting.
            queue_.Use();
            gldx::TransformFeedback& tf = tf_[static_cast<std::size_t>(p)];
            tf.Bind();
            glEnable(GL_RASTERIZER_DISCARD);
            tf.Begin(GL_POINTS);
            DrawFrontier(p);
            tf.End();
            glDisable(GL_RASTERIZER_DISCARD);
            tf.Unbind();
        }

        // The frontier the last pass left behind is at full depth: with uRemaining = 0
        // every cube in it counts as finished, so this draw closes the sponge. At level 0
        // nothing was captured at all and this is just the root cube.
        draw_.Use();
        draw_.Set("uRemaining", 0);
        Repoint(level_ == 0 ? seed_ : ping_[static_cast<std::size_t>(level_ - 1) % 2]);
        DrawFrontier(level_);
        cubeVao_.Unbind();

        if (hadDiscard == GL_TRUE) glEnable(GL_RASTERIZER_DISCARD);
        else                       glDisable(GL_RASTERIZER_DISCARD);
    }

    // Queue sizes for the HUD. Every read goes through the non-blocking availability
    // query, so none of them can stall a frame; reading a count in order to decide what
    // to draw instead would put a GPU sync in the render path and hand back exactly the
    // CPU-side knowledge this arm exists to avoid. The numbers are display-only.
    //
    // Two things about where this runs. It has to be at the *start* of a frame rather
    // than right after End(), because that is what makes the gauge trustworthy: a query
    // issued in the same frame as its capture answers "not yet" whenever the GPU is even
    // one frame behind, which is precisely the heavy levels where the queue size matters
    // most - measured, level 6 and up reported nothing at all for every frame of a run
    // while level 4 reported fine. After a vsynced frame boundary the previous frame's
    // queries have retired, and the read still never waits. And it reads all passes, not
    // just the last one, because two numbers matter: `frontier_`, the queue the closing
    // draw rendered, and `peakQueue_`, the largest queue that had to be captured, which
    // is the one approaching kQueueBudget. A pass's output is only charged against the
    // budget if another pass reads it as input, hence the p + 1 < level_ test - and the
    // root always counts as one, since pass 0 captures whatever it is given.
    void Harvest() {
        if (level_ == 0) {
            frontier_  = 1;
            peakQueue_ = 1;
            return;
        }
        std::int64_t peak = 1;
        std::int64_t last = 0;
        for (int p = 0; p < level_; ++p) {
            const gldx::TransformFeedback& tf = tf_[static_cast<std::size_t>(p)];
            if (!tf.PrimitivesAvailable()) return;   // keep last frame's numbers
            const std::int64_t n = static_cast<std::int64_t>(tf.PrimitivesGenerated());
            if (p + 1 < level_ && n > peak) peak = n;
            last = n;
        }
        frontier_  = last;
        peakQueue_ = peak;
    }

    void DrawHud(const gldx::RenderFrame& f) {
        if (!font_.loaded() || !white_.valid() || f.fbWidth <= 0 || f.fbHeight <= 0) return;

        char line[192];
        if (subdivide_) {
            std::snprintf(line, sizeof(line),
                          "uLevel %d  ·  %lld final cubes  ·  queue %lld/%lld%s  ·  minpx %.1f  ·  %.0f fps",
                          level_, static_cast<long long>(frontier_),
                          static_cast<long long>(peakQueue_), static_cast<long long>(kQueueBudget),
                          peakQueue_ >= kQueueBudget ? " FULL" : "", minPx_, f.smoothedFps);
        } else {
            std::snprintf(line, sizeof(line),
                          "uLevel %d  ·  %lld sub-cubes  ·  cull %s  ·  %.0f fps",
                          level_, static_cast<long long>(CubeCount(level_)),
                          cull_ ? "on" : "off", f.smoothedFps);
        }
        const float size  = 20.0f;
        const float width = gldx::TextRenderer::Measure(font_, line, size) + 20.0f;

        sprite_.Begin(white_, f.fbWidth, f.fbHeight);
        sprite_.Draw(white_, 14.0f, 14.0f, width, size + 14.0f,
                     0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));
        gldx::TextRenderer::Draw(sprite_, font_, line, 24.0f, 20.0f, size,
                                 glm::vec4(0.75f, 0.9f, 1.0f, 1.0f));
        gldx::TextRenderer::Draw(sprite_, font_,
                                 "Up/Down or +/- subdivision   Space orbit   H hud   C cull   G queue",
                                 24.0f, 20.0f + size + 16.0f, size * 0.7f,
                                 glm::vec4(0.6f, 0.65f, 0.72f, 0.9f));
        sprite_.End();
    }

    gldx::ShaderProgram exact_;
    gldx::ShaderProgram queue_;   // captures the children of cubes still worth splitting
    gldx::ShaderProgram draw_;    // expands the finished cubes of the same buffer
    gldx::VertexArray   idVao_;     // exhaustive arm: bound for legality, no attributes
    gldx::VertexArray   cubeVao_;   // buffer-fed draws: attribute 0 = one vec4 per cube
    gldx::GLBuffer      seed_;      // the root cube, never a capture destination
    gldx::GLBuffer      ping_[2];   // the queue itself
    std::array<gldx::TransformFeedback, kMaxLevelSubdivided> tf_;
    gldx::SpriteBatch   sprite_;
    gldx::Font          font_;
    gldx::Texture2D     white_;
    int         level_     = 3;
    float       size_      = 2.0f;
    float       minPx_     = 6.0f;
    bool        hud_       = true;
    bool        cull_      = true;
    bool        subdivide_ = true;
    std::int64_t frontier_ = 0;     // last landed count of the queue's tail, HUD only
    std::int64_t peakQueue_ = 0;    // last landed largest captured queue, HUD only
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {"subdivide", "cull", "hud", "spin"},
                                 {"level", "size", "distance", "minpx"}, "menger_sponge");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    // The arm decides the level ceiling, so it has to be read before the level is.
    const bool  subdivide = flags.on("subdivide");
    // integer()/real() rather than a cast of number(): argv can legitimately carry
    // "inf" / "nan" / "1e300" there and turning one of those into an int is
    // undefined behaviour, so the saturation has to happen before the conversion.
    const int   requested = flags.integer("level", 3);
    const int   level     = ClampLevel(requested, subdivide);
    const float size      = flags.real("size", 2.0f, 0.1f, 100.0f);
    const float distance  = flags.real("distance", 0.0f, 0.0f, 500.0f);
    // Lower bound 0 is a real setting, not a mistake: --minpx 0 removes the LOD stop and
    // lets the queue subdivide to the full level everywhere, which is the fastest way to
    // watch the budget guard start dropping cubes.
    const float minPx     = flags.real("minpx", 6.0f, 0.0f, 512.0f);
    if (level != requested)
        std::fprintf(stderr, "menger_sponge: --level saturates to %d (%s arm; %s)\n",
                     level, subdivide ? "subdivided" : "exhaustive",
                     subdivide ? "the ceiling there is the queue budget, not the level"
                               : "cost is 20^level, so the bound is the draw, not the shader");

    const bool cull = flags.on("cull");
    if (!cull && !subdivide && level >= kMaxLevelExact)
        std::fprintf(stderr, "menger_sponge: --off cull at level %d asks for %lld geometry-stage "
                             "vertices; the run will be slow, not stuck\n",
                     level, static_cast<long long>(CubeCount(level) * kVertsPerCube));

    bool spin = flags.on("spin");

    // Frame-time accumulator, written only from the render thread's frame callback.
    int   frames = 0;
    double sumDt = 0.0;
    double minDt = 1e300;

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - menger_sponge (geometry generated in the vertex + geometry stages)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    int exitCode = 0;

    // Declared after the window, so the pass and its GL objects are destroyed
    // before glfwDestroyWindow runs (the context is still current then).
    gldx::Renderer renderer;
    auto pass = std::make_unique<MengerPass>();
    MengerPass* raw = pass.get();
    renderer.AddPass(std::move(pass));

    gldx::Camera camera;
    camera.SetPerspective(45.0f, 1.0f, 0.1f, 100.0f);

    window.OnCreate([&](gldx::win::Window& w) {
        gldx::RenderContext::MarkAsRenderThread();
        renderer.Init();
        if (!raw->Create(level, size, flags.on("hud"), cull, subdivide, minPx)) {
            exitCode = 1;
            w.Close();
        }
    });

    window.OnKey([&](gldx::win::Window&, gldx::win::Key key,
                     gldx::win::KeyAction action, int) {
        if (action != gldx::win::KeyAction::Press) return;
        switch (key) {
            case gldx::win::Key::Up:
            case gldx::win::Key::Equal:   raw->addLevel(+1); break;
            case gldx::win::Key::Down:
            case gldx::win::Key::Minus:   raw->addLevel(-1); break;
            case gldx::win::Key::Space:   spin = !spin;      break;
            case gldx::win::Key::H:       raw->setHud(!raw->hud()); break;
            case gldx::win::Key::C:       raw->setCull(!raw->cull()); break;
            case gldx::win::Key::G:       raw->setSubdivide(!raw->subdivide()); break;
            default: break;
        }
    });

    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        const float aspect = info.fbHeight > 0
                                 ? static_cast<float>(info.fbWidth) / static_cast<float>(info.fbHeight)
                                 : 1.0f;
        // The orbit radius follows the sponge size so any --size stays framed; an
        // explicit --distance overrides it. Near/far ride along to keep depth
        // precision where the sub-cubes actually are.
        const float radius = distance > 0.0f ? distance : size * 1.9f;
        // Elapsed seconds, not glfwGetTime(): the frame is a pure function of the
        // clock, so two runs with the same switches must be pixel-identical.
        const float yaw = kBaseYaw + (spin ? static_cast<float>(info.time) * kSpinRate : 0.0f);
        const glm::vec3 eye(radius * std::cos(yaw), radius * kPitchUp, radius * std::sin(yaw));

        camera.SetViewportAspect(aspect);
        camera.SetPerspective(45.0f, aspect, radius * 0.02f, radius * 20.0f);
        camera.LookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        gldx::RenderFrame frame;
        frame.fbWidth     = info.fbWidth;
        frame.fbHeight    = info.fbHeight;
        frame.smoothedFps = info.smoothedFps;
        frame.camera      = &camera;
        frame.viewProj    = camera.ViewProjection();

        renderer.Render(frame);

        ++frames;
        sumDt += info.dt;
        if (info.dt > 0.0 && info.dt < minDt) minDt = info.dt;
    });

    const int rc = gldx::win::App::Get().Run({flags.quitAfter()});

    // Wall-clock frame statistics, printed once after the loop. The window is
    // vsynced, so the mean is floored at the refresh interval and any frame the GPU
    // finishes early reads as the same 16-odd milliseconds; the mean only starts to
    // move once the work no longer fits a frame. That is exactly the regime the
    // cull buys headroom into, so the number to read here is the minimum: pinned to
    // vsync it means the level fits, above it means the geometry stage ran out of
    // budget. A cheap level therefore shows cull on and off identical, which is
    // correct and not a failed measurement.
    if (frames > 0) {
        std::printf("menger_sponge: %d frames, mean %.2f ms, min %.2f ms "
                    "(cull %s, %s arm, level %d, %lld cubes in the closing draw, queue "
                    "peaked at %lld of %lld)\n",
                    frames, 1000.0 * sumDt / static_cast<double>(frames),
                    1000.0 * minDt, cull ? "on" : "off",
                    subdivide ? "subdivide" : "exhaustive", level,
                    subdivide ? static_cast<long long>(raw->frontierCubes()) : 0LL,
                    subdivide ? static_cast<long long>(raw->peakQueue()) : 0LL,
                    subdivide ? static_cast<long long>(kQueueBudget) : 0LL);
    }
    return exitCode ? exitCode : rc;
}
