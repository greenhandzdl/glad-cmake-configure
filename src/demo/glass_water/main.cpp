/**
 * @file main.cpp
 * @brief glass_water: a free-flight scene of glass, water and lamps, used to
 *        watch the depth buffer either protect or destroy what is behind a
 *        transparent surface.
 *
 * The question this demo is built around is the one every first transparency
 * attempt hits: a blended fragment writes depth, GL_LESS rejects everything
 * further along the ray, and the object behind the glass is simply gone. The fix
 * is not one line but a stack of four independent ones - don't write depth from
 * the blend layer, draw that layer far to near (at a granularity that actually
 * matches the geometry), give closed shells two passes, and pick a blend
 * equation that agrees with what the shader pre-multiplies. Each of the four has
 * its own key and its own command-line name, so switching one off shows exactly
 * which guarantee it was providing. See BlendPass.h for what each one buys.
 *
 * Controls:
 *   W A S D      fly          Space / LeftControl   up / down
 *   LeftShift    brake (0.35x - the errors are best read one metre away)
 *   mouse        look (while the pointer is captured)
 *   1            sort far -> near          2   blend layer writes depth
 *   3            closed shells two-pass    4   straight / premultiplied / additive
 *   [ ]          sea state   - =            flight speed
 *   C            back-face culling         V    wireframe
 *   B            bloom       H              HUD    F    free / grab the cursor
 *   X            quit        Esc            release the cursor only
 *
 * The same switches answer to the command line, so a sweep can produce the
 * correct frame and each of the four broken ones without a keyboard:
 *
 *   ./output/glass_water --off hud --snapshot output/glass_water.png
 *   ./output/glass_water --off hud --on depthwrite --off sort --snapshot eaten.png
 *   ./output/glass_water --off hud --off sort --snapshot unsorted.png
 *
 * Two conditions make those pairs usable as evidence, and both are easy to get
 * wrong. `--freeze-at SEC` must name a moment *at or before* `--snap-at`: the
 * wave clock is parked at the freeze time, so a snapshot taken earlier than that
 * still catches a live sea and two runs of one setting differ wherever the
 * surface ripples. And `--off hud` is not cosmetic - the overlay prints a live
 * fps number, so leaving it in puts a text diff into every comparison. With both
 * honoured, two runs of one configuration are bit-identical, and the pixel count
 * between two configurations is a measurement rather than a coincidence.
 *
 * This is deliberately a small C++ project rather than one large file: the item
 * table, the tiled water sheet and the CPU textures live in Scene.{h,cpp}, the
 * camera pose and the switches in Input.{h,cpp}, the pipeline state in
 * BlendPass.{h,cpp}, and the instrument panel in Hud.{h,cpp}. main.cpp only wires
 * them into the engine's render-pass pipeline and owns the camera.
 */

// Platform.h is deliberately a plain text include (not part of module gldx): it
// pulls <glad/gl.h> before <GLFW/glfw3.h> and defines the GLFW_PLATFORM_* macros
// this executable's #if checks rely on.
#include "gldx/core/Platform.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// The whole engine as a single C++20 named module, plus the window and CLI libs.
import gldx;
import gldxwin;
import gldxcli;

// Demo-local headers. They name gldx engine types, so (like Platform.h for the
// GLFW side) they are textual includes that must come AFTER `import gldx;`.
#include "Input.h"
#include "Scene.h"
#include "BlendPass.h"
#include "Hud.h"

using namespace glass_water;

namespace {

constexpr const char* kAppVersion   = "1.5.0";
constexpr const char* kWindowTitle  = "gldx::Renderer - glass, water, blending & depth";
constexpr int kWindowWidth  = 800;
constexpr int kWindowHeight = 600;

// --blend NAME, resolved against the three equations the pass can be set to. An
// unrecognised name is reported rather than quietly ignored, because a typo in a
// regression script would otherwise be recorded as a passing configuration.
//
// The fallback is the pairing the fragment stage is written against. The coverage
// now comes out of conservation rather than being authored, so which equation is
// live decides how the shader has to hand over its rgb (BlendPass.cpp keeps the
// two in step); a typo in a regression script should land on the documented
// default rather than on whichever entry happens to be first in the enum.
BlendMode ParseBlendMode(const std::string& text) {
    if (text == "straight" || text == "s") return BlendMode::Straight;
    if (text == "premultiplied" || text == "premul" || text == "p") return BlendMode::Premultiplied;
    if (text == "additive" || text == "add" || text == "a") return BlendMode::Additive;
    std::fprintf(stderr, "glass_water: unknown blend '%s' (straight|premultiplied|additive)\n",
                 text.c_str());
    return BlendMode::Premultiplied;
}

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv,
                            {"bloom", "hud", "sort", "depthwrite", "shell", "cull",
                             "wireframe", "glow", "capture", "ortho"},
                            {"blend", "yaw", "pitch", "pos-x", "pos-y", "pos-z",
                             "speed", "wave", "fov", "ortho-height", "freeze-at",
                             "snapshot", "snap-at"},
                            "glass_water");
    if (flags.wantsHelp()) {
        flags.printUsage();
        return 0;
    }

    gldx::win::WindowDesc desc;
    desc.width  = kWindowWidth;
    desc.height = kWindowHeight;
    desc.title  = kWindowTitle;
    gldx::win::Window window(desc);
    if (!window.Ok()) {
        std::cerr << "Failed to create GLFW window (OpenGL 4.1 core?)\n";
        return 1;
    }
    // Esc belongs to the pointer grab here (it frees the cursor so the OS can be
    // used again), so gldxwin's default Esc-closes-window has to stay off; X is
    // the deliberate quit key and Input.cpp handles it.
    window.SetCloseOnEsc(false);

    gldx::RenderContext::MarkAsRenderThread();
    std::printf("glass_water %s\n", kAppVersion);
    std::printf("OpenGL %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    Input input;
    // The four correctness switches, from the command line. Defaults are the
    // correct configuration, so `--off` names the guarantee under test and an
    // unset run is the reference frame.
    input.sortBackToFront  = flags.on("sort");
    input.blendWritesDepth = flags.on("depthwrite", false);
    input.shellTwoPass     = flags.on("shell");
    input.blend            = ParseBlendMode(flags.string("blend", "premultiplied"));
    // Projection. Off by default, and --on ortho is the scripted way to shoot the
    // flat view: it removes the perspective gradient the far shore's grazing
    // angles come from, which makes it the control for the Fresnel term.
    input.ortho        = flags.on("ortho", false);
    input.fov          = flags.real("fov", input.fov, 8.0f, 110.0f);
    input.orthoHeight  = flags.real("ortho-height", input.orthoHeight, 2.0f, 80.0f);
    // Diagnostics.
    input.cull       = flags.on("cull");
    input.wireframe  = flags.on("wireframe", false);
    input.hud        = flags.on("hud");
    input.bloom      = flags.on("bloom");
    input.glow       = flags.on("glow");
    input.yaw        = flags.real("yaw", input.yaw);
    input.pitch      = flags.real("pitch", input.pitch);
    input.pos.x      = flags.real("pos-x", input.pos.x);
    input.pos.y      = flags.real("pos-y", input.pos.y);
    input.pos.z      = flags.real("pos-z", input.pos.z);
    input.speed      = flags.real("speed", input.speed, 0.5f, 40.0f);
    input.wave       = flags.real("wave", input.wave, 0.0f, 0.6f);

    // A scripted run gets a free cursor: nothing is moving the mouse, and a
    // grabbed pointer on someone's desktop is a rude way to find that out.
    // `--on/--off capture` overrides either way.
    const std::string snapPath = flags.string("snapshot");
    const bool scripted = !snapPath.empty();
    ApplyCapture(window, input, !flags.on("capture", !scripted));

    // Input arrives through the gldxwin input surface: gldxwin owns the GLFW
    // trampolines, so the demo records its state pointer and subscribes.
    window.SetUserData(&input);
    window.OnCursor(MouseCallback);
    // The wheel belongs to the projection, not to the flight: it zooms whichever
    // framing is live (see ScrollCallback).
    window.OnScroll(ScrollCallback);

    // All GPU-resource owners live inside this lambda so their destructors run
    // when it returns - while the GL context is still current, before the window
    // is destroyed. The Window object outlives the lambda, so no context is lost
    // first.
    auto runDemo = [&]() -> int {
        gldx::Renderer renderer;
        renderer.Init();

        // The demo replaces the library's geometry pass with its own layered one,
        // so the pipeline is assembled by hand instead of BuildPbrPipeline():
        // scene -> post -> HUD, nothing else.
        auto blendUp = std::make_unique<BlendPass>();
        BlendPass* blendPass = blendUp.get();
        if (!blendPass->Init()) {
            std::fprintf(stderr, "glass_water: shader pipeline failed, nothing to draw\n");
            return 1;
        }
        renderer.AddPass(std::move(blendUp));
        renderer.AddPass(std::make_unique<gldx::PostProcessPass>());

        auto hudUp = std::make_unique<GlassHudPass>();
        GlassHudPass* hudPass = hudUp.get();
        hudPass->blend = blendPass;
        hudPass->in = &input;
        renderer.AddPass(std::move(hudUp));

        // ---- post-process chain + 2D HUD infrastructure ----------------------
        // The blended layers have to accumulate in a linear float target: eight-bit
        // integers round every intermediate and clip the additive lamps, so 1 + 1
        // stops being 2 and the whole point of the ordering goes unnoticed.
        gldx::PostProcessChain post;
        if (!post.Init()) {
            std::fprintf(stderr, "PostProcessChain init failed\n");
            return 1;
        }
        post.SetExposure(1.0f);

        gldx::SpriteBatch sprite;
        if (!sprite.Init()) {
            std::fprintf(stderr, "SpriteBatch init failed\n");
            return 1;
        }

        gldx::Font font;
        for (int i = 0; i < kFontCandidateCount; ++i) {
            if (font.LoadFromFile(kFontCandidates[i], 48.0f)) break;
        }
        if (!font.loaded()) std::fprintf(stderr, "HUD font not found; text overlay disabled\n");

        gldx::Texture2D white;
        white.Upload(MakeWhiteDesc());

        gldx::Profiler profiler;
        profiler.Init();

        Scene scene;
        scene.Build();

        gldx::Camera camera;
        // Near 0.1: the panes are 6 cm thick and flying up to them is the demo's
        // whole point, so a conservative near plane would clip them away. The same
        // pair of planes carries the ortho box too - under a flat projection depth
        // is linear between them, so 200 units of far plane costs no precision.
        camera.SetPerspective(input.fov, 1.0f, 0.1f, 200.0f);
        camera.SetYawPitch(input.yaw, input.pitch);
        camera.Translate(input.pos - camera.Position());

        // The water sheet's middle, i.e. the thing the camera is overwhelmingly
        // pointed at. Switching projections has to keep framing it the same size
        // or Tab reads as a zoom glitch rather than as a projection change.
        constexpr glm::vec3 kViewTarget{0.0f, 0.0f, -14.0f};
        bool wasOrtho = input.ortho;

        const double quitAfter = flags.quitAfter();
        const double freezeAt = flags.number("freeze-at");
        const double snapAt = flags.real("snap-at", 2.0, 0.1, 120.0);
        bool snapped = false;

        window.OnFrame([&](gldx::win::FrameInfo& info) {
            gldx::win::Window& win = *info.window;
            // Clamp before anything moves: a dragged window or a driver hiccup can
            // hand back a third of a second, and at 40 u/s that teleports the
            // camera straight through a pane - which reads as a transparency bug.
            const float dt = static_cast<float>(std::clamp(info.dt, 0.0, 0.1));
            const float aspect = info.fbHeight > 0
                ? static_cast<float>(info.fbWidth) / static_cast<float>(info.fbHeight)
                : 1.0f;

            HandleKeys(win, input, dt);
            const FlyIntent fly = ReadFlyIntent(win, input, dt);

            camera.SetViewportAspect(aspect);
            // The demo owns the pose, the camera owns the matrices. Input is
            // written by keyboard and mouse alike, so it is pushed in each frame
            // and read back out afterwards - one position, no second source of
            // truth for the HUD or the sort key.
            camera.SetYawPitch(input.yaw, input.pitch);
            camera.Translate(input.pos - camera.Position());
            camera.MoveForward(fly.forward);
            camera.MoveRight(fly.strafe);
            camera.MoveUp(fly.rise);
            input.pos = camera.Position();

            // The projection is re-asserted every frame from Input rather than set
            // once on the keypress, because three separate things feed it: the
            // resize above, the wheel, and Tab. Whichever one moved last, the camera
            // ends the frame in the mode the HUD is about to report.
            if (input.ortho != wasOrtho) {
                wasOrtho = input.ortho;
                // Handing over the framing on the flip, so nothing else in the
                // frame changes size: the ortho box gets the height the perspective
                // frustum had at the water's own distance, and going back the fov
                // resumes from the value it was before - not from whatever the wheel
                // did to the box in between. Same act, two units; a switch that
                // resized the scene would be indistinguishable from a bug.
                if (input.ortho) {
                    const float d = glm::max(glm::length(camera.Position() - kViewTarget), 1.0f);
                    input.orthoHeight = 2.0f * d
                        * std::tan(glm::radians(input.fov) * 0.5f);
                }
            }
            if (input.ortho) camera.SetOrthographic(input.orthoHeight, aspect, 0.1f, 200.0f);
            else             camera.SetPerspective(input.fov, aspect, 0.1f, 200.0f);

            // --freeze-at S parks the wave clock S seconds after the loop starts,
            // which is what makes two runs of one setting pixel-identical.
            const double animTime = freezeAt > 0.0 ? std::min(info.time, freezeAt) : info.time;

            BlendPass::Settings& s = blendPass->settings;
            s.scene = &scene;
            s.sortBackToFront  = input.sortBackToFront;
            s.blendWritesDepth = input.blendWritesDepth;
            s.shellTwoPass     = input.shellTwoPass;
            s.blend            = input.blend;
            s.cull       = input.cull;
            s.wireframe  = input.wireframe;
            s.glow       = input.glow;
            s.wave       = input.wave;
            s.time       = static_cast<float>(animTime);
            s.sunToward  = scene.sunToward;
            // The pass does not read the camera, so the projection arrives as these
            // two: the mode for the BRDF's viewing direction, and the forward vector
            // it uses when there is no eye position to subtract.
            s.orthographic = input.ortho;
            s.camForward   = camera.Forward();

            gldx::RenderFrame frame;
            frame.camera = &camera;
            frame.viewProj = camera.ViewProjection();
            frame.post = &post;
            frame.useBloom = input.bloom;
            frame.overlay.sprite = &sprite;
            frame.overlay.font = &font;
            frame.overlay.white = &white;
            frame.overlay.profiler = &profiler;
            frame.fbWidth = info.fbWidth;
            frame.fbHeight = info.fbHeight;
            frame.smoothedFps = info.smoothedFps;

            profiler.BeginFrame();
            renderer.Render(frame);

            // Verification hook: dump the rendered framebuffer to a PNG once the
            // scene has settled, so a blank window can never masquerade as a clean
            // exit. Only active when --snapshot PATH is given.
            if (!snapPath.empty() && !snapped && info.time >= snapAt) {
                snapped = true;
                // Window-bound capture via the gldxwin member, like every other
                // demo. The free gldx::CaptureScreenshot lives in module-gldx
                // purview and does not resolve across the static-lib boundary on
                // MSVC's module linker (macOS/Linux ld tolerate it).
                if (info.window->CaptureScreenshot(snapPath))
                    std::printf("snapshot: %s\n", snapPath.c_str());
                else
                    std::fprintf(stderr, "snapshot failed: %s\n", snapPath.c_str());
                info.window->Close();
            }
        });

        const int rc = gldx::win::App::Get().Run({quitAfter});
        // The counters are the last frame actually submitted, which is what a
        // scripted sweep asserts on: sorted, the blend layer should report the
        // sheet's tiles among its submissions; unsorted it should not.
        std::printf("glass_water: %d blend submissions, %d total draws on the last frame\n",
                    blendPass->blendDraws(), blendPass->totalDraws());
        return rc;
        // Every GPU-resource owner destructs as runDemo returns, on the render
        // thread and while the window's context is still current.
    };

    return runDemo();
}
