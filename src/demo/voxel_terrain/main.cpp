/**
 * @file main.cpp
 * @brief voxel_terrain — the playable acceptance test for gldx's voxel primitives.
 *
 * Everything engine-side lives in module gldx (chunks, the mesher, the texture
 * array, the voxel passes, the DDA raycast, noise, particles, fog). What is
 * *game* side — the chunk grid, the streaming policy, the terrain generator,
 * the block editing rules and the HUD — is built here, on top of the library,
 * which is the point: the engine must not need a world layer of its own to
 * render one.
 *
 * Like the other big demos this is a small C++ project, not one file: the world
 * layer (terrain / trees / water cellular automaton / block texture array / the
 * World + WaterSim + GenerateChunk) lives in World.{h,cpp}, the fly/orbit camera
 * and its GLFW callbacks in Input.{h,cpp}, and the HUD pass in Hud.{h,cpp}.
 * main.cpp keeps the wiring and the per-frame loop that drives them.
 *
 * Pipeline (a custom pass order rather than the default one):
 *   VoxelOpaquePass -> VoxelTransparentPass -> PostProcessPass -> VoxelHudPass
 *
 * Threading follows the engine's two-phase convention: worker threads run the
 * terrain generation and the chunk meshing (pure CPU, `ChunkMesher`), and the
 * render thread drains the completed jobs and uploads them, a few per frame,
 * so a world edit or a fast flight never stalls the frame. Workers read block
 * data under a shared lock; edits take the exclusive lock (see World::mx).
 *
 * Controls:
 *   mouse        : look around (pointer captured)
 *   W/A/S/D      : fly forward / back / left / right
 *   Space / Ctrl : fly up / down          hold LeftShift : slower flight
 *   left click   : break the aimed block (debris particles)
 *   right click  : place the selected block on the aimed face
 *   1..8         : choose the block type
 *   F            : toggle fly / orbit camera (ESC releases the pointer, or
 *                  quits while orbiting)       X : quit
 *   [ / ]        : sun azimuth                - / = : sun elevation
 *   P            : toggle the debris particles
 *   B            : toggle two-sided terrain (skips back-face culling, so the
 *                  far walls of a dig stay visible when you look from below)
 *   Tab          : toggle perspective / orthographic projection
 *   scroll       : zoom the orbit camera (orbit mode)
 *
 * Every renderer feature can also be switched from the command line, which is
 * what lets a script diff one feature at a time without a keyboard: see
 * the gldxcli module and `--help` for the list (--off fog,water,sky,particles,
 * --on ortho, --auto-break N, --quit-after SECONDS). --yaw / --pitch / --rise
 * aim and lift the fly camera: at the spawn tilt the crosshair ray lands past
 * the interaction reach, so scripted mining needs a steeper pitch, and
 * --auto-place N with --select 7 builds the water a water test cannot find on
 * its own in this part of the world. --on double-sided mirrors the B key. The
 * fly camera now collides with terrain (a dug shaft can be descended to its
 * floor, and water above a broken cell pours down to fill it). --freeze-at also
 * leaves the pointer alone:
 * a captured pointer lets whoever is moving the mouse next to the window rewrite
 * --yaw / --pitch, and the scripted edit counts with it.
 *
 * The window is created with the same 4.1-core hints as the PBR demo; see the
 * pbr_showcase demo for the engine's other showcase.
 */

// Platform.h stays a plain text include (not part of module gldx): it orders
// <glad/gl.h> before <GLFW/glfw3.h> and defines the GLFW_PLATFORM_* macros.
#include "gldx/core/Platform.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// The whole engine as a single C++20 named module, plus the window and CLI libs.
import gldx;
import gldxwin;
import gldxcli;

// Demo-local headers. They name gldx engine types, so (like Platform.h for the
// GLFW side) they are textual includes that must come AFTER `import gldx;`.
#include "World.h"
#include "Input.h"
#include "Hud.h"
#include "Streaming.h"

using namespace voxel_terrain;

// Application identity + default window geometry now live with the demo, not
// the engine header (Platform.h no longer carries app-level constants).
constexpr const char* kAppVersion   = "1.4.0";
constexpr const char* kWindowTitle  = "gldx::Renderer - voxel playground";
constexpr int kWindowWidth  = 800;
constexpr int kWindowHeight = 600;

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv,
                            {"particles", "fog", "water", "sky", "ortho", "double-sided"},
                            {"auto-break", "auto-place", "freeze-at", "yaw", "pitch", "rise",
                             "select"}, "voxel_terrain");
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
    // Voxel drives ESC itself: in fly mode it releases/takes the pointer grab, and
    // only quits while orbiting. So gldxwin's default Esc-to-close must stay off.
    window.SetCloseOnEsc(false);

    gldx::RenderContext::MarkAsRenderThread();
    std::printf("voxel_terrain %s\n", kAppVersion);
    std::printf("OpenGL %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    Input input;
    input.showParticles = flags.on("particles");
    input.showFog = flags.on("fog");
    input.showSky = flags.on("sky");
    // The water sheet is alpha-blended, so "off" means fully transparent rather
    // than a pass removed from the pipeline: the pass also draws the particles,
    // and the two switches have to stay independent.
    input.waterAlpha = flags.on("water") ? 0.85f : 0.0f;
    input.ortho = flags.on("ortho", false);
    input.doubleSided = flags.on("double-sided", false);
    // Which block --auto-place builds with; the sweep uses 7 (water) to put a
    // transparent sheet in the frame without needing a lake to be nearby.
    input.selected = std::clamp(flags.integer("select", input.selected),
                                1, static_cast<int>(Block::kBuiltinCount) - 1);
    // Input arrives through the gldxwin input surface: gldxwin owns the GLFW
    // trampolines, so the demo just records its state pointer and subscribes.
    window.SetUserData(&input);
    window.OnCursor(MouseCallback);
    window.OnMouseButton(MouseButtonCallback);
    window.OnScroll(ScrollCallback);

    // Same teardown contract as main.cpp: everything owning GL lives in this
    // lambda so destructors run while the context is current.
    auto runDemo = [&]() -> int {
        // ---- engine objects -------------------------------------------------
        gldx::Renderer renderer;
        renderer.Init();
        // No BuildPbrPipeline(): the voxel demo swaps GeometryPass for the
        // voxel pair, adds the standalone sky stage between opaque and
        // transparent (where the inline sky used to sit), and brings its own HUD.
        auto opaquePass = std::make_unique<gldx::VoxelOpaquePass>();
        auto transpPass = std::make_unique<gldx::VoxelTransparentPass>();
        auto hudPass = std::make_unique<VoxelHudPass>();
        gldx::VoxelOpaquePass* opaqueRaw = opaquePass.get();
        gldx::VoxelTransparentPass* transpRaw = transpPass.get();
        VoxelHudPass* hudRaw = hudPass.get();

        renderer.AddPass(std::move(opaquePass));
        renderer.AddPass(std::make_unique<gldx::SkyboxPass>());
        renderer.AddPass(std::move(transpPass));
        renderer.AddPass(std::make_unique<gldx::PostProcessPass>());
        renderer.AddPass(std::move(hudPass));

        auto voxelProg = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kVoxelVertex,
                                                             gldx::shaders::kVoxelFragment);
        if (!voxelProg) {
            std::fprintf(stderr, "Voxel shader error:\n%s\n", voxelProg.error().c_str());
            return 1;
        }
        voxelProg->Use();
        voxelProg->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);
        // Sampler-unit uniforms persist on the program object.
        voxelProg->Set("uAtlas", static_cast<int>(gldx::texunit::voxelAtlas));

        gldx::Texture2DArray atlas;
        atlas.Upload(MakeBlockAtlasDesc());

        gldx::SkyboxRenderer skybox;
        if (!skybox.Init()) {
            std::fprintf(stderr, "Skybox init failed\n");
            return 1;
        }
        // Procedural HDR sky + IBL, baked once from the initial sun (same
        // contract as main.cpp): [ ] and - = move the direct light, not the sun
        // disc in the sky - regenerating the cube per frame would cost more than
        // the feature is worth in a demo.
        const glm::vec3 initialTravel = -SunToward(input);
        gldx::EnvironmentMap env;
        if (!env.Generate(initialTravel, 256, 32, 256)) {
            std::fprintf(stderr, "Environment generation failed\n");
            return 1;
        }

        gldx::PostProcessChain post;
        if (!post.Init()) {
            std::fprintf(stderr, "PostProcessChain init failed\n");
            return 1;
        }
        post.SetExposure(1.15f);

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
        white.Upload([]() {
            gldx::Texture2DDesc d;
            d.width = d.height = 1;
            d.channels = 4;
            d.srgb = false;
            d.pixels = {255, 255, 255, 255};
            return d;
        }());

        gldx::ParticleBatch particles;
        if (!particles.Init()) std::fprintf(stderr, "ParticleBatch init failed\n");

        gldx::Profiler profiler;
        profiler.Init();

        gldx::LightBuffer lightBuffer;
        lightBuffer.Init();

        gldx::Camera camera;
        camera.SetPerspective(60.0f, 1.0f, 0.1f, 400.0f);
        // --on ortho gives the camera the one kick the Tab key would have; the
        // camera stays the projection authority and input.ortho mirrors it.
        if (input.ortho) camera.ToggleProjection();

        // ---- world + streaming state ---------------------------------------
        World world;
        WaterSim water;
        {
            // Spawn above the terrain at the world's centre. The fly camera is
            // the position authority, so the seed has to be pushed into it -
            // starting at the origin instead drops the player into the corner
            // of the world box, where the surface check then lifts them over
            // an empty chunk wall.
            const int sx = kWorldX / 2, sz = kWorldZ / 2;
            const int h = TerrainHeight(world.noise, sx, sz);
            // --rise is bounded by the world height, which also keeps the sum
            // below inside the int range however absurd the argument is. It may
            // go negative: the sweep starts one block lower than the default
            // spawn to keep the freshly dug shaft centred in the frame.
            const int rise = std::clamp(flags.integer("rise"), -kWorldY, kWorldY);
            input.focus = glm::vec3(
                static_cast<float>(sx) + 0.5f,
                static_cast<float>(std::max(h, kSeaLevel) + 6 + rise),
                static_cast<float>(sz) + 0.5f);
            input.yaw   = flags.real("yaw", input.yaw);
            input.pitch = flags.real("pitch", input.pitch);
            camera.SetYawPitch(input.yaw, input.pitch);
            camera.Translate(input.focus - camera.Position());
        }

        gldx::VoxelPipeline vx;
        vx.prog = &*voxelProg;
        vx.atlas = &atlas;
        vx.chunks = &world.gpu;
        // The atlas slice is already 0.72 alpha; the pass multiplies, so 0.85
        // lands the water sheet near two-thirds opacity - enough to read as
        // liquid while still showing the sand bottom through it. --off water
        // brings that down to 0 (see the input setup above).
        vx.waterAlpha = input.waterAlpha;
        opaqueRaw->SetPipeline(vx);
        transpRaw->SetPipeline(vx);

        // Generation + meshing workers, the two in-flight queues and the
        // upload drain all live behind one call: Streamer.{h,cpp}.
        ChunkStreamer streamer(world);
        int& genCount = streamer.genCountRef();
        int& meshCount = streamer.meshCountRef();

        const bool scripted = flags.number("freeze-at") > 0.0;
        ApplyCapture(window, input, scripted);
        const double startedAt = gldx::win::App::Now();
        const double quitAfter = flags.quitAfter();
        const double freezeAt = flags.number("freeze-at");
        // Everything time-driven (flight speed, water scroll, debris) advances on
        // this clock rather than on wall time, so --freeze-at can park it.
        double animTime = startedAt;
        double lastAnimTime = animTime;
        // Debris integrates on a fixed 120 Hz step counted off that clock instead
        // of the frame delta. Two reasons, one of them a test: with a variable
        // step, the parked burst sat at a different height in every run, which put
        // the noise floor of a particles-off screenshot diff at 2% of the frame
        // and made the switch unmeasurable; in play it means the same puff falls
        // identically at 30 and at 300 fps.
        constexpr double kSimStep = 1.0 / 120.0;
        int simSteps = 0;
        double smoothedFps = 60.0;
        // Fixed-window frame counter for the HUD readout (see the loop).
        int fpsFrames = 0;
        double fpsWindowStart = startedAt;
        static const char* const kBlockKeys[9] = {
            "", "1 grass", "2 dirt", "3 stone", "4 sand",
            "5 wood", "6 leaves", "7 water", "8 snow"
        };
        // The HUD shows the previous frame's cull counters: the passes write
        // them into the frame, which is assembled after the text is built.
        int prevVisible = 0, prevTotal = 0;
        // --auto-break N / --auto-place N: scripted mining and building, for the
        // headless feature sweep. Each tick goes through exactly the same path a
        // click does (DDA pick -> Edit -> remesh -> debris), so a screenshot taken
        // after it says something about all four without a keyboard attached.
        // Scripted edits tick every 0.25 s of fixed-step time, so their cadence is
        // as reproducible as the debris they spawn. The upper bound is not
        // cosmetic: the two counters are summed below, so saturating straight at
        // INT_MAX would overflow that addition.
        constexpr int kMaxScriptedEdits = 100000;
        int autoBreakLeft = std::clamp(flags.integer("auto-break"), 0, kMaxScriptedEdits);
        int autoPlaceLeft = std::clamp(flags.integer("auto-place"), 0, kMaxScriptedEdits);
        constexpr int kBreakPeriodSteps = 30;
        int nextBreakStep = 240;                              // first tick 2 s in
        int spawnedTotal = 0, frames = 0, breaks = 0;
        double minFps = 1e9;

        window.OnFrame([&](gldx::win::FrameInfo& info) {
            gldx::win::Window& win = *info.window;
            // ---- edge-detected toggles + keys -------------------------------
            auto edge = [](bool down, bool& armed) {
                if (down && armed) { armed = false; return true; }
                if (!down) armed = true;
                return false;
            };
            static bool flyArmed = true, escArmed = true, partArmed = true, orthoArmed = true;
            static bool bsArmed = true;
            if (edge(win.KeyIsDown(gldx::win::Key::F), flyArmed)) {
                input.cam = (input.cam == Input::Cam::Fly) ? Input::Cam::Orbit
                                                            : Input::Cam::Fly;
                if (input.cam == Input::Cam::Orbit) input.focus = camera.Position();
                ApplyCapture(window, input, scripted);
            }
            if (edge(win.KeyIsDown(gldx::win::Key::Escape), escArmed)) {
                if (input.cam == Input::Cam::Fly) {
                    input.captured = !input.captured;
                    win.SetCursorCaptured(input.captured);
                } else {
                    info.window->Close();
                }
            }
            if (edge(win.KeyIsDown(gldx::win::Key::P), partArmed))
                input.showParticles = !input.showParticles;
            if (win.KeyIsDown(gldx::win::Key::X))
                info.window->Close();
            // Block hotbar 1..8: the portable Key enum has no contiguous digit
            // codes, so the eight keys are an explicit table.
            static const gldx::win::Key kBlockKeys2[8] = {
                gldx::win::Key::Num1, gldx::win::Key::Num2, gldx::win::Key::Num3,
                gldx::win::Key::Num4, gldx::win::Key::Num5, gldx::win::Key::Num6,
                gldx::win::Key::Num7, gldx::win::Key::Num8,
            };
            for (int k = 0; k < 8; ++k) {
                if (win.KeyIsDown(kBlockKeys2[k]))
                    input.selected = k + 1;
            }
            if (edge(win.KeyIsDown(gldx::win::Key::Tab), orthoArmed)) {
                // The camera owns the projection state; the demo only mirrors
                // the result here for the HUD line.
                input.ortho = camera.ToggleProjection()
                              == gldx::Camera::Projection::Orthographic;
            }
            if (edge(win.KeyIsDown(gldx::win::Key::B), bsArmed))
                input.doubleSided = !input.doubleSided;

            const double now = gldx::win::App::Now();
            // --freeze-at S parks the demo clock S seconds after start: dt falls to
            // 0, so the scroll, the debris and the scripted breaks all stop, and
            // two runs with the same settings become pixel-identical for a sweep.
            // The clamp is applied to *elapsed* seconds rather than to an absolute
            // timestamp: (startedAt + S) - startedAt is not exactly S in binary
            // floating point, and the lost ulp was enough to make the fixed-step
            // count below flicker between 959 and 960 - one step of debris, a few
            // pixels of pure noise in every screenshot pair.
            const double elapsed = freezeAt > 0.0 ? std::min(now - startedAt, freezeAt)
                                                 : now - startedAt;
            animTime = startedAt + elapsed;
            const float dt = static_cast<float>(std::min(animTime - lastAnimTime, 0.25));
            lastAnimTime = animTime;
            // Catch the fixed-step clock up with the demo clock. Both are clamped
            // by --freeze-at, so the step count reached at the parked moment is a
            // function of S alone and no frame pacing can shift it. The one thing
            // pacing can still shift is *where a frame starts*: a slow frame
            // advances two or three steps at once and can jump clean over a
            // scripted-edit boundary, leaving that burst a step older here than
            // there. So the boundary is used as an intermediate target - the loop
            // stops on it, the edit below runs, and the rest of the frame's steps
            // are taken afterwards.
            const int targetSteps = static_cast<int>(elapsed / kSimStep + 0.5);
            int stepGoal = targetSteps;
            if (autoBreakLeft + autoPlaceLeft > 0)
                stepGoal = std::min(targetSteps, nextBreakStep);
            while (simSteps < stepGoal) {
                ++simSteps;
                particles.Update(static_cast<float>(kSimStep));
            }
            // Averaged over a fixed wall-clock window instead of an EMA of 1/dt
            // per frame: the latter is dominated by whichever frame last
            // stalled, so the number never agreed with the frame it was drawn on.
            ++fpsFrames;
            if (const double span = now - fpsWindowStart; span >= 0.5) {
                smoothedFps = static_cast<float>(fpsFrames) / static_cast<float>(span);
                minFps = std::min(minFps, static_cast<double>(smoothedFps));
                fpsFrames = 0;
                fpsWindowStart = now;
            }
            ++frames;

            // ---- sun direction ([ ] azimuth, - = elevation) ------------------
            // Polled rather than edge-triggered: re-aiming the sun is a dragging
            // sort of job, and scaling by dt keeps the sweep rate the same on a
            // 30 fps laptop as on a 300 fps one.
            constexpr float kSunRate = 0.6f;
            if (win.KeyIsDown(gldx::win::Key::LeftBracket))
                input.sunAzimuth -= kSunRate * dt;
            if (win.KeyIsDown(gldx::win::Key::RightBracket))
                input.sunAzimuth += kSunRate * dt;
            if (win.KeyIsDown(gldx::win::Key::Minus))
                input.sunElevation = std::max(0.05f, input.sunElevation - kSunRate * dt);
            if (win.KeyIsDown(gldx::win::Key::Equal))
                input.sunElevation = std::min(1.45f, input.sunElevation + kSunRate * dt);

            const gldx::win::Vec2d fb = win.FramebufferSize();
            camera.SetViewportAspect(fb.y > 0 ? static_cast<float>(fb.x) / static_cast<float>(fb.y) : 1.0f);
            camera.SetOrbitRadius(input.orbitRadius);

            // ---- movement ---------------------------------------------------
            const bool boost = win.KeyIsDown(gldx::win::Key::LeftShift);
            const float step = input.speed * (boost ? 0.35f : 1.0f) * dt;
            if (input.cam == Input::Cam::Fly) {
                camera.SetYawPitch(input.yaw, input.pitch);
                // Build the intended displacement from the camera basis instead
                // of moving the eye directly, so it can be resolved against the
                // world: the same Forward/Right/Up axes MoveForward et al use.
                glm::vec3 delta(0.0f);
                if (win.KeyIsDown(gldx::win::Key::W)) delta += camera.Forward();
                if (win.KeyIsDown(gldx::win::Key::S)) delta -= camera.Forward();
                if (win.KeyIsDown(gldx::win::Key::A)) delta -= camera.Right();
                if (win.KeyIsDown(gldx::win::Key::D)) delta += camera.Right();
                if (win.KeyIsDown(gldx::win::Key::Space)) delta += camera.Up();
                if (win.KeyIsDown(gldx::win::Key::LeftControl)) delta -= camera.Up();
                delta *= step;

                // The eye sits kEyeHeight above the feet; the collider moves the
                // feet, so terrain under the player is what stops the descent (a
                // real floor) rather than the old sea-level teleport that made
                // the camera refuse to go down below the water line.
                const gldx::VoxelBody playerBody{0.3f, 1.9f};
                glm::vec3 feet = camera.Position();
                feet.y -= kEyeHeight;
                // Sub-step so one resolve never advances the body past half a
                // cell: a stalled frame's dt clamp would otherwise let a full
                // step several cells long tunnel thin walls (see Collision.h).
                const float len = glm::length(delta);
                const int segs = len > 0.5f ? static_cast<int>(std::ceil(len / 0.5f)) : 1;
                const glm::vec3 seg = delta / static_cast<float>(segs);
                {
                    std::shared_lock<std::shared_mutex> lk(world.mx);
                    for (int s = 0; s < segs; ++s)
                        gldx::MoveVoxelAabb(feet, playerBody, seg,
                                           [&world](glm::ivec3 c) { return world.Pickable(c); });
                }
                feet.y += kEyeHeight;
                // Keep the eye inside the world box even where chunks are not
                // streamed in yet (there the collider sees air and cannot stop).
                feet.x = std::clamp(feet.x, 1.0f, static_cast<float>(kWorldX) - 1.0f);
                feet.z = std::clamp(feet.z, 1.0f, static_cast<float>(kWorldZ) - 1.0f);
                feet.y = std::clamp(feet.y, 1.0f + kEyeHeight, static_cast<float>(kWorldY) - 2.0f);
                camera.Translate(feet - camera.Position());
            } else {
                const float cp = std::cos(input.orbitPitch);
                const glm::vec3 eye(
                    input.focus.x + input.orbitRadius * cp * std::sin(-input.orbitYaw),
                    input.focus.y + input.orbitRadius * std::sin(input.orbitPitch),
                    input.focus.z + input.orbitRadius * cp * std::cos(-input.orbitYaw));
                camera.LookAt(eye, input.focus, glm::vec3(0, 1, 0));
            }

            // ---- block editing (DDA pick through the screen centre) ---------
            bool scriptedBreak = false;
            if (autoBreakLeft + autoPlaceLeft > 0 && simSteps >= nextBreakStep) {
                // Re-anchor on the *absolute* cadence (period multiples from the
                // first tick), not on "now + period": the latter let the frame
                // that happened to cross the 2 s mark shift every later tick by
                // however many steps it had jumped, so the last scripted break
                // fell inside or outside --freeze-at depending on frame pacing.
                // Ticks missed during a stall are dropped, never replayed.
                nextBreakStep += kBreakPeriodSteps;
                if (nextBreakStep <= simSteps) nextBreakStep = simSteps + kBreakPeriodSteps;
                if (autoBreakLeft > 0) {
                    --autoBreakLeft;
                    input.wantBreak = true;
                    scriptedBreak = true;
                } else {
                    --autoPlaceLeft;
                    input.wantPlace = true;
                }
            }
            if (input.wantBreak || input.wantPlace) {
                const gldx::Ray ray = gldx::PickRay(static_cast<float>(fb.x) * 0.5f,
                                                  static_cast<float>(fb.y) * 0.5f,
                                                  static_cast<int>(fb.x), static_cast<int>(fb.y),
                                                  camera.InverseViewProjection());
                std::optional<gldx::VoxelHit> hit;
                {
                    std::shared_lock<std::shared_mutex> lk(world.mx);
                    hit = gldx::RaycastVoxel(
                        ray, glm::ivec3{0, 0, 0},
                        glm::ivec3{kWorldX - 1, kWorldY - 1, kWorldZ - 1},
                        [&world](glm::ivec3 c) { return world.Pickable(c); }, kReach);
                }
                if (hit) {
                    const BlockId broken = [&]() {
                        std::shared_lock<std::shared_mutex> lk(world.mx);
                        return world.Sample(hit->position);
                    }();
                    if (input.wantBreak) {
                        ++breaks;   // reported at exit: proves the scripted sweep mined
                        world.Edit(hit->position, 0);
                        water.schedule(hit->position);   // water above may pour in
                        // Debris: a short outward burst, tinted by the block's
                        // own slice colour so the puff reads as "that material".
                        const glm::vec3 tint = BlockTint(broken);
                        const glm::vec3 c = glm::vec3(hit->position) + 0.5f;
                        for (int i = 0; i < 22 && input.showParticles; ++i) {
                            // Seeded off the break counter, not the clock: an
                            // animTime-derived seed changed wholesale whenever the
                            // frame happened to land on a different tenth of a
                            // second, which made each puff irreproducible.
                            const std::uint32_t h = Hash2(i * 7, breaks * 31 + i);
                            const glm::vec3 v(
                                (static_cast<float>(h & 255) / 255.0f - 0.5f) * 4.0f,
                                1.2f + static_cast<float>((h >> 8) & 255) / 255.0f * 2.4f,
                                (static_cast<float>((h >> 16) & 255) / 255.0f - 0.5f) * 4.0f);
                            if (particles.Spawn(c + 0.4f * glm::vec3(hit->normal), v,
                                                glm::vec4(tint, 1.0f), 0.55f, 0.14f))
                                ++spawnedTotal;
                        }
                        // Drop the eye with the floor just removed: a shaft dug
                        // under the feet otherwise gets deeper than the
                        // interaction reach and the remaining ticks would pick
                        // empty air.
                        if (scriptedBreak) camera.Translate({0.0f, -1.0f, 0.0f});
                    }
                    if (input.wantPlace) {
                        const glm::ivec3 target = hit->position + hit->normal;
                        // Never seal the player inside a block.
                        const glm::ivec3 eyeCell{static_cast<int>(camera.Position().x),
                                                 static_cast<int>(camera.Position().y),
                                                 static_cast<int>(camera.Position().z)};
                        if (std::abs(target.x - eyeCell.x) + std::abs(target.y - eyeCell.y)
                                + std::abs(target.z - eyeCell.z) > 1) {
                            world.Edit(target, static_cast<BlockId>(input.selected));
                            water.schedule(target);
                        }
                    }
                }
                input.wantBreak = input.wantPlace = false;
            }
            // The steps this frame skipped on the way to the boundary.
            while (simSteps < targetSteps) {
                ++simSteps;
                particles.Update(static_cast<float>(kSimStep));
            }

            streamer.Stream(camera.Position());
            water.step(world, dt);

            // ---- lighting + fog ---------------------------------------------
            const glm::vec3 towardSun = SunToward(input);
            gldx::LightSetup setup;
            setup.sun.direction = -towardSun;
            setup.sun.color = glm::vec3(1.0f, 0.96f, 0.88f);
            setup.sun.intensity = 2.2f;
            setup.ambient = glm::vec3(0.30f, 0.34f, 0.40f);
            // Fog closes just inside the render distance, so chunk pop-in
            // happens behind the haze rather than in front of the camera.
            // Leaving fogStart at 0 is what switches it off in the shader.
            setup.fogColor = ToLinear(glm::vec3(0.60f, 0.72f, 0.88f));
            if (input.showFog) {
                setup.fogStart = static_cast<float>(kRenderDistance) * kChunk * 0.35f;
                setup.fogEnd   = static_cast<float>(kRenderDistance) * kChunk * 0.98f;
            }
            lightBuffer.Update(setup, camera.Position());

            const glm::mat4 viewProj = camera.ViewProjection();
            gldx::Frustum frustum;
            frustum.Extract(viewProj);

            vx.time = static_cast<float>(elapsed);
            vx.doubleSided = input.doubleSided;

            char line[512];
            std::snprintf(line, sizeof(line),
                          "voxel_terrain %s  %s\n"
                          "%.0f fps   CPU %.2f ms  GPU %.2f ms   chunks %d (meshed %d)  pos %.0f %.0f %.0f\n"
                          "streaming gen %zu mesh %zu   particles %zu   visible %d/%d\n"
                          "picked: %s   (WASD fly, space/ctrl up/down, shift slow, LMB break, RMB place)\n"
                          "F camera %s   ESC pointer %s   P particles %s   B 2-sided %s   [ ] - = sun   X quit",
                          kAppVersion,
                          input.cam == Input::Cam::Fly ? "FLY" : "ORBIT",
                          smoothedFps,
                          profiler.CpuMs(), profiler.GpuMs(),
                          genCount, meshCount,
                          camera.Position().x, camera.Position().y, camera.Position().z,
                          streamer.genInFlight(), streamer.meshInFlight(), particles.count(),
                          prevVisible, prevTotal,
                          kBlockKeys[std::min(input.selected, 8)],
                          input.cam == Input::Cam::Fly ? "-> orbit" : "-> fly",
                          input.captured ? "captured" : "free",
                          input.showParticles ? "on" : "off",
                          input.doubleSided ? "on" : "off");
            hudRaw->text = line;
            hudRaw->crosshair = (input.cam == Input::Cam::Fly);

            gldx::RenderFrame frame;
            frame.camera = &camera;
            frame.frustum = &frustum;
            frame.viewProj = viewProj;
            frame.post = &post;
            frame.lights = &lightBuffer;
            frame.lightSetup = setup;
            frame.shadow.sunToward = towardSun;
            frame.sky.env = &env;
            frame.sky.box = input.showSky ? &skybox : nullptr;
            frame.overlay.sprite = &sprite;
            frame.overlay.font = &font;
            frame.overlay.white = &white;
            frame.overlay.profiler = &profiler;
            frame.particles = input.showParticles ? &particles : nullptr;
            frame.useBloom = false;
            frame.fbWidth = static_cast<int>(fb.x);
            frame.fbHeight = static_cast<int>(fb.y);
            frame.smoothedFps = smoothedFps;
            frame.ortho = input.ortho;

            profiler.BeginFrame();
            renderer.Render(frame);
            prevVisible = frame.visibleCount;
            prevTotal   = frame.totalNodes;

        });

        const int rc = gldx::win::App::Get().Run({quitAfter});

        // One summary line per run: the counters a scripted sweep or a CI smoke
        // cannot read back out of a screenshot (streaming convergence in
        // particular - the HUD shows them, but only to whoever is looking).
        const double ranFor = gldx::win::App::Now() - startedAt;
        std::printf("voxel_terrain: ran %.1fs  %d frames  fps avg %.1f min %.1f   "
                    "chunks gen %d meshed %d   blocks broken %d   particles live %zu spawned %d\n",
                    ranFor, frames,
                    ranFor > 0.0 ? static_cast<double>(frames) / ranFor : 0.0,
                    minFps > 1e8 ? 0.0 : minFps,
                    genCount, meshCount, breaks, particles.count(), spawnedTotal);

        streamer.Shutdown();
        return rc;   // GPU owners destruct here, on the render thread
    };

    return runDemo();
}
