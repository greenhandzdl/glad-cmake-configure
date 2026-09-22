/**
 * @file main.cpp
 * @brief Phase 5 demo: the HDR/PBR/CSM/IBL renderer now organised as a scene
 *        graph (Scene/SceneNode/Transform) driven through an ordered render-pass
 *        pipeline (Renderer + Shadow/Geometry/PostProcess/DebugHud passes), plus
 *        frustum culling, a DebugDraw overlay, CPU+GPU profiling, mouse picking,
 *        and a GPU-instanced field surfaced through the sprite-batched text HUD.
 *
 * Controls:
 *   drag mouse   : orbit          scroll      : zoom
 *   A/D or Left/Right : sun azimuth           W/S or Up/Down : sun elevation
 *   right click  : pick an object (bounding-sphere ray test)
 *   1 : cascaded shadows   2 : IBL   3 : bloom   4 : debug lines   5 : instanced field
 *   6 : sky background
 *   Tab : toggle perspective / orthographic projection
 *   Esc : quit
 *
 * The same features answer to the command line (--off shadow,ibl, --help for
 * the list, --quit-after SECONDS to end a scripted run), so a regression sweep
 * can switch each one without a keyboard: see the gldxcli module. --yaw / --pitch /
 * --radius aim the orbit camera, which a sweep needs to bring the instanced
 * field and the sky into the frame at all; --freeze-at SECONDS parks the
 * animation clock, which is what makes two runs of one setting pixel-identical.
 *
 * The application owns the GL resources (meshes / materials / textures / passes);
 * scene nodes reference them by non-owning pointer, so the whole frame's draw
 * sequence - once a wall of inline gl* calls here - is now a single
 * renderer.Render(frame). All GL still happens on the render thread.
 *
 * This is deliberately a small C++ project, not one big file: the orbit-camera
 * state and its gldxwin input-surface callbacks live in Input.{h,cpp}, and the scene graph +
 * instanced field + the CPU texture generators in Scene.{h,cpp}. main.cpp stays
 * the wiring that stitches the engine together around them.
 */

// Platform.h is deliberately a plain text include (not part of module gldx):
// it pulls <glad/gl.h> before <GLFW/glfw3.h> and defines the GLFW_PLATFORM_*
// macros this executable's #if checks rely on.
#include "gldx/core/Platform.h"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

// The whole engine as a single C++20 named module: no per-header includes.
import gldx;
import gldxwin;
import gldxcli;

// Demo-local headers. They name gldx engine types, so (like Platform.h for the
// GLFW side) they are textual includes that must come AFTER `import gldx;`.
#include "Input.h"
#include "Scene.h"

using namespace pbr_showcase;

// Application identity + default window geometry now live with the demo, not
// the engine header (Platform.h no longer carries app-level constants).
constexpr const char* kAppName      = "GLFW + GLAD gldx Engine";
constexpr const char* kAppVersion   = "1.4.0";
constexpr const char* kWindowTitle  = "gldx::Renderer - PBR / scene graph / render passes";
constexpr int kWindowWidth  = 800;
constexpr int kWindowHeight = 600;

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv,
                            {"shadow", "ibl", "bloom", "debug", "instances", "sky", "ortho"},
                            {"freeze-at", "yaw", "pitch", "radius", "snapshot", "snap-at"}, "pbr_showcase");
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

    gldx::RenderContext::MarkAsRenderThread();

    std::printf("%s %s\n", kAppName, kAppVersion);
    std::printf("OpenGL %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    Input input;
    // Aim the orbit camera from the command line. Needed because two of the
    // toggles have nothing to show at the default view: the instanced field sits
    // out toward +x (yaw about -pi/2) and the sky only enters the frame once the
    // pitch goes negative. Unset, these are the Input defaults, byte for byte.
    input.yaw   = flags.real("yaw", input.yaw);
    input.pitch = flags.real("pitch", input.pitch);
    input.radius = flags.real("radius", input.radius, 2.0f, 60.0f);
    input.useShadow = flags.on("shadow");
    input.useIbl = flags.on("ibl");
    input.useBloom = flags.on("bloom");
    input.useDebug = flags.on("debug", false);
    input.useInstances = flags.on("instances", false);
    input.useSky = flags.on("sky");
    input.ortho = flags.on("ortho", false);
    // Input arrives through the gldxwin input surface: gldxwin owns the GLFW
    // trampolines, so the demo just records its state pointer and subscribes.
    window.SetUserData(&input);
    window.OnCursor(MouseCallback);
    window.OnMouseButton(MouseButtonCallback);
    window.OnScroll(ScrollCallback);

    // All GPU-resource owners live inside this lambda so their destructors run
    // when it returns — while the GL context is still current, before the window
    // is destroyed. A failure path just `return 1;`; the single exit below owns
    // the glfwDestroyWindow / glfwTerminate teardown, so no context is lost first.
    auto runDemo = [&]() -> int {
        gldx::Renderer renderer;
        renderer.Init();
        renderer.BuildPbrPipeline();

        auto pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex, gldx::shaders::kPbrFragment);
        if (!pbr) {
            std::fprintf(stderr, "PBR shader error:\n%s\n", pbr.error().c_str());
            return 1;
        }
        auto depth = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kDepthVertex, gldx::shaders::kDepthFragment);
        if (!depth) {
            std::fprintf(stderr, "Depth shader error:\n%s\n", depth.error().c_str());
            return 1;
        }

        gldx::SkyboxRenderer skybox;
        if (!skybox.Init()) {
            std::fprintf(stderr, "Skybox init failed\n");
            return 1;
        }

        // Procedural HDR sky + IBL precomputes, baked once from the initial sun.
        const glm::vec3 initialTravel = -SunToward(input);
        gldx::EnvironmentMap env;
        if (!env.Generate(initialTravel, 256, 32, 256)) {
            std::fprintf(stderr, "Environment generation failed\n");
            return 1;
        }

        // ---- post-process chain + 2D HUD infrastructure ----------------------
        gldx::PostProcessChain post;
        if (!post.Init()) {
            std::fprintf(stderr, "PostProcessChain init failed\n");
            return 1;
        }
        post.SetExposure(1.1f);

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
        white.Upload(MakeSolidDesc());

        // ---- debug overlay, profiler, GPU-instanced field --------------------
        gldx::DebugDraw debug;
        if (!debug.Init()) std::fprintf(stderr, "DebugDraw init failed\n");

        gldx::Profiler profiler;
        profiler.Init();

        auto instProg = gldx::ShaderProgram::CreateFromSource(
            gldx::shaders::kInstancedVertex, gldx::shaders::kInstancedFragment);
        if (!instProg) {
            std::fprintf(stderr, "Instanced shader error:\n%s\n", instProg.error().c_str());
        } else {
            instProg->Use();
            instProg->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);
        }
        gldx::InstancedMesh instField;   // CPU data staged (Stage A), uploaded once (Stage B)
        BuildInstancedField(instField);

        // ---- scene graph (owned + built inside ShowcaseScene) ----------------
        ShowcaseScene world;
        world.Build();
        gldx::Scene& scene = world.scene();
        gldx::SceneNode* carousel = world.carousel();

        gldx::LightBuffer lightBuffer;
        lightBuffer.Init();
        gldx::CascadedShadowMap csm;
        csm.Init(2048);

        gldx::Camera camera;
        camera.SetPerspective(45.0f, 1.0f, 0.1f, 200.0f);
        // The camera owns the projection state; --on ortho just gives it the one
        // kick the Tab key would have, and input.ortho mirrors the result for the
        // HUD (same arrangement as the per-frame toggle below).
        if (input.ortho) camera.ToggleProjection();

        const glm::vec3 target(0.0f, 0.6f, 0.0f);

        // Sampler-unit uniforms persist on the program; set them once.
        pbr->Use();
        pbr->Set("uShadowMap", static_cast<int>(gldx::texunit::shadowArray));
        pbr->Set("uIrradiance", static_cast<int>(gldx::texunit::irradiance));
        pbr->Set("uPrefilter", static_cast<int>(gldx::texunit::prefilter));
        pbr->Set("uBrdfLut", static_cast<int>(gldx::texunit::brdfLut));
        // GLSL 4.10: map the std140 blocks onto the fixed UBO binding points
        // that LightBuffer / CascadedShadowMap bind their buffers to each frame.
        pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);
        pbr->SetBlockBinding("ShadowBlock", gldx::CascadedShadowMap::kShadowBinding);

        gldx::SceneNode* selected = nullptr;
        const double quitAfter = flags.quitAfter();
        const double freezeAt = flags.number("freeze-at");
        // Verification hook: dump the rendered framebuffer to a PNG once the scene
        // has settled, so a blank window can never masquerade as a clean exit.
        // Only active when --snapshot PATH is given.
        const std::string snapPath = flags.string("snapshot");
        const double snapAt = flags.real("snap-at", 2.0, 0.1, 120.0);
        bool snapped = false;

        window.OnFrame([&](gldx::win::FrameInfo& info) {
            gldx::win::Window& win = *info.window;
            // Edge-detect the 1..6 toggles: flip once per press, rearm on release.
            auto edgeToggle = [](bool pressed, bool& armed, bool& flag) {
                if (pressed && armed) { flag = !flag; armed = false; }
                else if (!pressed) armed = true;
            };
            static bool shadowArmed = true, iblArmed = true, bloomArmed = true,
                        debugArmed = true, instArmed = true, skyArmed = true, projArmed = true;
            edgeToggle(win.KeyIsDown(gldx::win::Key::Num1), shadowArmed, input.useShadow);
            edgeToggle(win.KeyIsDown(gldx::win::Key::Num2), iblArmed,    input.useIbl);
            edgeToggle(win.KeyIsDown(gldx::win::Key::Num3), bloomArmed,  input.useBloom);
            edgeToggle(win.KeyIsDown(gldx::win::Key::Num4), debugArmed,  input.useDebug);
            edgeToggle(win.KeyIsDown(gldx::win::Key::Num5), instArmed,   input.useInstances);
            edgeToggle(win.KeyIsDown(gldx::win::Key::Num6), skyArmed,    input.useSky);
            // Tab swaps perspective <-> orthographic once per press.
            if (bool tabDown = win.KeyIsDown(gldx::win::Key::Tab); tabDown && projArmed) {
                input.ortho = camera.ToggleProjection()
                              == gldx::Camera::Projection::Orthographic;
                projArmed = false;
            } else if (!tabDown) {
                projArmed = true;
            }
            HandleKeys(win, input);

            const int fbw = info.fbWidth;
            const int fbh = info.fbHeight;
            camera.SetViewportAspect(fbh > 0 ? static_cast<float>(fbw) / fbh : 1.0f);
            camera.SetOrbitRadius(input.radius);   // keeps the ortho box matched to the orbit

            const float cp = std::cos(input.pitch);
            const glm::vec3 eye(
                target.x + input.radius * cp * std::sin(input.yaw),
                target.y + input.radius * std::sin(input.pitch),
                target.z + input.radius * cp * std::cos(input.yaw));
            camera.LookAt(eye, target, glm::vec3(0, 1, 0));

            const glm::vec3 towardSun = SunToward(input);
            const glm::vec3 sunTravel = -towardSun;

            gldx::LightSetup setup;
            setup.sun.direction = sunTravel;
            setup.sun.color = glm::vec3(1.0f);
            setup.sun.intensity = 3.0f;
            setup.ambient = input.useIbl ? glm::vec3(0.0f) : glm::vec3(0.05f);
            lightBuffer.Update(setup, camera.Position());

            // Advance the animated hierarchy, then refresh world transforms once.
            // --freeze-at S parks the animation clock S seconds after start, which
            // is what makes two runs of the same settings pixel-identical for a
            // screenshot sweep; unset, the clock is plain wall time as always.
            // The carousel turns on seconds *since start*, not the absolute
            // timer: the clock counts from machine boot, so an absolute clock
            // left the parked phase different on every run (and lost precision on
            // a long-lived desktop, where the float cast alone moved the angle by
            // thousandths of a radian per frame).
            const double animElapsed =
                freezeAt > 0.0 ? std::min(info.time, freezeAt) : info.time;
            carousel->local().SetAxisAngle(glm::vec3(0, 1, 0),
                                           static_cast<float>(animElapsed) * 0.6f);
            scene.Update();

            const glm::mat4 viewProj = camera.ViewProjection();
            gldx::Frustum frustum;
            frustum.Extract(viewProj);

            // Consume a pending right-click pick against the fresh world spheres.
            if (input.pickPending) {
                input.pickPending = false;
                const auto& targets = scene.PickTargets();
                std::vector<std::pair<glm::vec3, float>> spheres;
                spheres.reserve(targets.size());
                for (const auto& t : targets) spheres.emplace_back(t.center, t.radius);
                const gldx::Ray ray = gldx::PickRay(
                    input.pickX * static_cast<float>(fbw),
                    input.pickY * static_cast<float>(fbh), fbw, fbh,
                    camera.InverseViewProjection());
                const int idx = gldx::PickNearest(ray, spheres);
                selected = (idx >= 0) ? targets[idx].node : nullptr;
            }

            // Assemble the frame and run the whole pipeline.
            gldx::RenderFrame frame;
            frame.camera = &camera;
            frame.frustum = &frustum;
            frame.scene = &scene;
            frame.viewProj = viewProj;
            frame.post = &post;
            frame.lights = &lightBuffer;
            frame.pbr = &*pbr;
            frame.lightSetup = setup;
            // Optional subsystems, each attached only because this demo brings it.
            frame.sky.box = input.useSky ? &skybox : nullptr;
            frame.sky.env = &env;
            frame.shadow.map = &csm;
            frame.shadow.depth = &*depth;
            frame.shadow.sunToward = towardSun;
            frame.shadow.enabled = input.useShadow;
            frame.ibl.enabled = input.useIbl;
            frame.instances.field = &instField;
            frame.instances.prog = instProg ? &*instProg : nullptr;
            frame.instances.enabled = input.useInstances;
            frame.overlay.debug = &debug;
            frame.overlay.sprite = &sprite;
            frame.overlay.font = &font;
            frame.overlay.white = &white;
            frame.overlay.profiler = &profiler;
            frame.overlay.useDebug = input.useDebug;
            frame.useBloom = input.useBloom;
            frame.ortho = input.ortho;
            frame.selected = selected;
            frame.fbWidth = fbw;
            frame.fbHeight = fbh;
            frame.smoothedFps = info.smoothedFps;

            profiler.BeginFrame();
            renderer.Render(frame);

            if (!snapPath.empty() && !snapped && info.time >= snapAt) {
                snapped = true;
                if (gldx::CaptureScreenshot(snapPath))
                    std::printf("snapshot: %s\n", snapPath.c_str());
                else
                    std::fprintf(stderr, "snapshot failed: %s\n", snapPath.c_str());
                info.window->Close();
            }
        });

        return gldx::win::App::Get().Run({quitAfter});
        // Every GPU-resource owner destructs as runDemo returns, on the render
        // thread and while the window's context is still current (the Window
        // object outlives this lambda, so glfwDestroyWindow has not run yet).
    };

    return runDemo();
}
