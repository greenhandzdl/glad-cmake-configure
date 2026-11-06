/**
 * @file main.cpp
 * @brief night_forest: a moonlit clearing of a thousand instanced trees, a crate
 *        that starts wandering on its own and mirrors the world back out of an FBO
 *        cube map, and a sky that is a cube map too.
 *
 * The claim this demo is built around is about draw calls. A fir is a trunk and
 * three faceted cones - forty triangles of geometry that could just as easily be
 * forty objects - and the field is 1400 of them, two species, plus a ground sheet,
 * four lanterns and one crate. It all arrives in five instanced draws because the
 * per-tree difference (offset, spin, one uniform scale, one tint) is a *buffer*
 * rather than a command, and the per-part difference (bark brown against needle
 * green) is a vertex colour that the engine's InstancedMesh now forwards to
 * location 9. `Scene.cpp` is that argument; the HUD prints the two numbers that
 * settle it.
 *
 * The second half is the crate. It is the only object with an environment mix, and
 * its cube is not a picture of a forest - it is six FBO renders of the world from
 * its own centre, refreshed whenever it moves. Which is also the reason it moves at
 * all: a reflection of a static scene is a static reflection, and only a box that
 * turns corners shows whether the cube is really following it.
 *
 * The lighting is a Gooch directional model rather than the engine's PBR program,
 * because a physically-based response to moonlight is a black silhouette. The
 * terminator is an edge between two flat tones, the shadow tone is a cold tint
 * rather than zero, and the highlight is added and greyed - see Shaders.h.
 *
 * Controls:
 *   W A S D      fly          Space / LeftControl   up / down
 *   LeftShift    brake (0.35x, for reading a reflection at close range)
 *   mouse        look (while the pointer is captured)   wheel  field of view
 *   1            FBO environment capture     2   refraction as well as reflection
 *   3            lantern point lights        4   night mist
 *   R            release / park the wandering crate
 *   G            walk the scripted path instead of flying (climbs the terrain)
 *   [ ]          env mix     - =               flight speed
 *   C            back-face culling            V  wireframe
 *   B            bloom       H                HUD
 *   F            free / grab the cursor       X  quit   Esc  release the cursor
 *
 * The same switches answer to the command line, so the comparison frames can be
 * produced without a keyboard:
 *
 *   ./output/night_forest --off hud --freeze-at 8 --snap-at 9 \
 *       --snapshot output/night_forest.png
 *   ./output/night_forest --off hud --off capture --freeze-at 8 --snap-at 9 \
 *       --snapshot crate-mirrors-sky.png
 *   ./output/night_forest --off hud --env-mix 0.5 --env-lod 0 \
 *       --snapshot crate-is-a-mirror.png
 *
 * The ground is the third axis (experiment manual 3.1 / 3.2 / 3.4): it stays the
 * flat sheet unless `--terrain` names a mode, and the walking camera is the way to
 * photograph it from inside rather than above.
 *
 *   ./output/night_forest --terrain hills --off hud --freeze-at 8 --snap-at 9 \
 *       --snapshot rolling-ridges.png
 *   ./output/night_forest --terrain contour --terrain-map height.raw \
 *       --terrain-levels 4 --on walk --freeze-at 24 --snap-at 25 \
 *       --snapshot terraced-contour-walk.png
 *
 * Two conditions make those pairs usable as evidence, and both are easy to get
 * wrong. `--freeze-at SEC` parks the clock, and the crate's wander is a table of
 * poses generated from the seed at a fixed 0.1 s step - so the box is in the same
 * place in every run at the same second, which is what makes a randomly-moving
 * object screenshot-reproducible at all. And `--off hud` is not cosmetic: the panel
 * prints a live fps number, so leaving it in puts a text diff into every
 * comparison. Honour both and two runs of one configuration are bit-identical.
 *
 * This is deliberately a small C++ project rather than one large file: the trees,
 * the scatter and the roam table live in Scene.{h,cpp}, the star cube in
 * Sky.{h,cpp}, the flight camera and switches in Input.{h,cpp}, the six-face
 * capture and the world draw in Passes.{h,cpp}, and the instrument panel in
 * Hud.{h,cpp}. main.cpp only wires them into the engine's render-pass pipeline and
 * owns the camera and the lights.
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

// Demo-local headers. Scene.h / Passes.h / Hud.h name gldx engine types, so (like
// Platform.h for the GLFW side) they are textual includes that must come AFTER
// `import gldx;`.
#include "Input.h"
#include "Sky.h"
#include "Scene.h"
#include "Passes.h"
#include "Hud.h"

using namespace night_forest;

namespace {

constexpr const char* kAppVersion   = "1.5.0";
constexpr const char* kWindowTitle  = "gldx::Renderer - instanced night forest, Gooch light, FBO cube";
constexpr int kWindowWidth  = 800;
constexpr int kWindowHeight = 600;

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv,
                            {"capture", "refract", "lights", "fog", "roam", "hud",
                             "bloom", "cull", "wireframe", "cursor", "walk"},
                            {"stars", "star-size", "seed", "trees", "env-size",
                             "env-mix", "env-lod", "emissive", "ior", "band",
                             "moon-x", "moon-y", "moon-z", "moon-int", "density",
                             "start-at", "speed", "fov", "yaw", "pitch",
                             "pos-x", "pos-y", "pos-z", "freeze-at", "snapshot",
                             "snap-at", "terrain", "terrain-cells", "terrain-cell",
                             "terrain-relief", "terrain-smooth", "terrain-levels",
                             "terrain-map", "terrain-tile", "walk-radius",
                             "walk-speed", "walk-eye"},
                            "night_forest");
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
    // used again), so gldxwin's default Esc-closes-window has to stay off; X is the
    // deliberate quit key and Input.cpp handles it.
    window.SetCloseOnEsc(false);

    gldx::RenderContext::MarkAsRenderThread();
    std::printf("night_forest %s\n", kAppVersion);
    std::printf("OpenGL %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    Input input;
    // The five content switches. Defaults are the intended look, so `--off` names
    // the guarantee under test and an unset run is the reference frame.
    input.capture = flags.on("capture");
    input.refract = flags.on("refract");
    input.lights  = flags.on("lights");
    input.fog     = flags.on("fog");
    input.roam    = flags.on("roam");
    input.cull      = flags.on("cull");
    input.wireframe = flags.on("wireframe", false);
    input.hud   = flags.on("hud");
    input.bloom = flags.on("bloom");
    input.envMix = flags.real("env-mix", input.envMix, 0.0f, 1.0f);
    input.yaw    = flags.real("yaw", input.yaw);
    input.pitch  = flags.real("pitch", input.pitch);
    input.pos.x  = flags.real("pos-x", input.pos.x);
    input.pos.y  = flags.real("pos-y", input.pos.y);
    input.pos.z  = flags.real("pos-z", input.pos.z);
    input.speed  = flags.real("speed", input.speed, 0.5f, 40.0f);
    input.fov    = flags.real("fov", input.fov, 8.0f, 110.0f);

    // A scripted run gets a free cursor: nothing is moving the mouse, and a grabbed
    // pointer on someone's desktop is a rude way to find that out. `--on/--off
    // cursor` overrides either way. Named `cursor` rather than `capture` because in
    // this demo capture means the cube map.
    const std::string snapPath = flags.string("snapshot");
    const bool scripted = !snapPath.empty();
    ApplyCapture(window, input, !flags.on("cursor", !scripted));

    // ---- the sky and the field, from the seed -------------------------------
    StarField field;
    field.size      = flags.integer("star-size", field.size);
    field.seed      = static_cast<std::uint32_t>(flags.integer("seed", 20261104));
    field.density   = flags.real("density", field.density, 0.0f, 1.0f);
    field.moonDir   = glm::vec3(flags.real("moon-x", field.moonDir.x),
                                flags.real("moon-y", field.moonDir.y),
                                flags.real("moon-z", field.moonDir.z));
    field.moonIntensity = flags.real("moon-int", field.moonIntensity, 0.0f, 400.0f);

    Scene scene;
    scene.config.seed      = field.seed;      // one seed: the sky and the forest
    scene.config.treeCount = flags.integer("trees", scene.config.treeCount);
    scene.config.wander.startAt = flags.real("start-at",
                                             static_cast<float>(scene.config.wander.startAt),
                                             0.0f, 60.0f);

    // ---- the ground: the manual's 3.1 / 3.2 / 3.4, off unless named ---------
    // An unknown mode is reported rather than dropped into `flat`, because a
    // screenshot taken with a typo in it looks exactly like a screenshot of the
    // flat sheet and the documentation would then be citing the wrong experiment.
    TerrainDesc terrainDesc;
    terrainDesc.seed = field.seed;   // the same seed drives the sky and the hills
    const std::string terrainMode = flags.string("terrain");
    if (!terrainMode.empty() && !ParseTerrainMode(terrainMode, terrainDesc.mode)) {
        std::fprintf(stderr, "night_forest: --terrain '%s' is not one of"
                             " flat / random / hills / contour, drawing the"
                             " flat sheet\n", terrainMode.c_str());
        terrainDesc.mode = TerrainMode::flat;
    }
    terrainDesc.cells     = static_cast<std::uint32_t>(flags.integer("terrain-cells",
                                                                     static_cast<int>(terrainDesc.cells)));
    terrainDesc.cell      = flags.real("terrain-cell", terrainDesc.cell, 0.1f, 50.0f);
    terrainDesc.relief    = flags.real("terrain-relief", terrainDesc.relief, 0.0f, 200.0f);
    terrainDesc.smoothing = flags.integer("terrain-smooth", terrainDesc.smoothing);
    terrainDesc.levels    = flags.integer("terrain-levels", terrainDesc.levels);
    terrainDesc.heightMap = flags.string("terrain-map");
    scene.config.terrain  = terrainDesc;
    scene.config.terrainTile = flags.real("terrain-tile", scene.config.terrainTile, 0.0f, 200.0f);

    // The walking path (see Scene.h): its own pace and radius, so it can leave the
    // clearing and cross the tree band where the relief actually is.
    scene.config.walk.radius = flags.real("walk-radius", scene.config.walk.radius, 5.0f, 190.0f);
    scene.config.walk.speed  = flags.real("walk-speed", scene.config.walk.speed, 0.1f, 10.0f);
    scene.config.walkEye     = flags.real("walk-eye", scene.config.walkEye, 0.5f, 4.0f);
    input.walk = flags.on("walk", false);

    // Input arrives through the gldxwin input surface: gldxwin owns the GLFW
    // trampolines, so the demo records its state pointer and subscribes.
    window.SetUserData(&input);
    window.OnCursor(MouseCallback);
    window.OnScroll(ScrollCallback);

    // Stage A, before the context-dependent work: the star cube is painted on the
    // CPU as 6 x size^2 half-texels. Either the procedural field, or a 2:1
    // equirectangular panorama reprojected through the same face table the capture
    // renders with - which is the reason both paths put the moon in the same place.
    const std::string starsPath = flags.string("stars");
    gldx::TextureCubeDesc skyDesc;
    std::string skySource;
    std::string skyError;
    if (!starsPath.empty()) {
        if (LoadEquirectCubeDesc(starsPath, field, skyDesc, skyError)) {
            skySource = "sky: panorama " + starsPath +
                        " reprojected to " + std::to_string(skyDesc.size) + "^2";
        } else {
            std::fprintf(stderr, "night_forest: '%s' rejected (%s), painting the procedural field\n",
                         starsPath.c_str(), skyError.c_str());
            skyDesc = MakeStarCubeDesc(field);
            skySource = "sky: procedural (panorama failed)";
        }
    } else {
        skyDesc = MakeStarCubeDesc(field);
        skySource = "sky: procedural " + std::to_string(skyDesc.size)
                    + "^2 seed " + std::to_string(field.seed);
    }

    // All GPU-resource owners live inside this lambda so their destructors run
    // when it returns - while the GL context is still current, before the window
    // is destroyed. The Window object outlives the lambda, so no context is lost
    // first.
    auto runDemo = [&]() -> int {
        gldx::Renderer renderer;
        renderer.Init();

        // The demo replaces the library's geometry and skybox passes with its own
        // single pass, which does the six-face capture and the world draw together:
        // scene -> post -> HUD, nothing else.
        auto nightUp = std::make_unique<NightPass>();
        NightPass* nightPass = nightUp.get();
        if (!nightPass->Init()) {
            std::fprintf(stderr, "night_forest: shader pipeline failed, nothing to draw\n");
            return 1;
        }
        renderer.AddPass(std::move(nightUp));
        renderer.AddPass(std::make_unique<gldx::PostProcessPass>());

        auto hudUp = std::make_unique<NightHudPass>();
        NightHudPass* hudPass = hudUp.get();
        hudPass->pass  = nightPass;
        hudPass->scene = &scene;
        hudPass->in    = &input;
        hudPass->field = &field;
        hudPass->skySource = skySource;
        renderer.AddPass(std::move(hudUp));

        // ---- post-process chain + 2D HUD infrastructure ----------------------
        // The scene is authored in linear HDR: the moon disc and the lantern glass
        // are both above 1.0, and ACES is what turns that headroom into a sky you
        // can look at rather than two clipped white blobs.
        gldx::PostProcessChain post;
        if (!post.Init()) {
            std::fprintf(stderr, "PostProcessChain init failed\n");
            return 1;
        }
        post.SetExposure(1.0f);
        // Above the star field, not just above the lit side of a trunk. The stars
        // peak at three and there are fifteen hundred of them: keyed at 1.05 they
        // all bloom and the sky fogs over into a milky sheet, which is the opposite
        // of a point of light. At 2.0 only the moon disc and the lantern glass - the
        // two things in the frame that are actually *sources* - bleed, and the few
        // dozen brightest points clear the threshold by a third and get a glow worth
        // looking for.
        post.SetBloomThreshold(2.0f);
        post.SetBloomStrength(0.45f);

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

        gldx::Profiler profiler;
        profiler.Init();

        // Stage B: upload the geometry, the instance arrays and the two procedural
        // maps. Also where the scatter runs, so the seed alone decides the forest.
        scene.Build();

        gldx::TextureCubeMap sky;
        sky.Upload(skyDesc);

        gldx::SkyboxRenderer skybox;
        if (!skybox.Init()) {
            std::fprintf(stderr, "night_forest: skybox renderer failed\n");
            return 1;
        }

        gldx::LightBuffer lights;
        lights.Init();

        gldx::Camera camera;
        // Near 0.1 so the camera can be flown right up to the crate - the distance
        // at which a reflection stops being decoration and becomes the subject. Far
        // 420 covers the ground sheet (200 half-width) from the default spawn and
        // the tree line at 95 m with fog doing the rest.
        camera.SetPerspective(input.fov, 1.0f, 0.1f, 420.0f);
        camera.SetYawPitch(input.yaw, input.pitch);
        camera.Translate(input.pos - camera.Position());

        const double quitAfter = flags.quitAfter();
        const double freezeAt  = flags.number("freeze-at");
        const double snapAt    = flags.real("snap-at", 3.0, 0.1, 120.0);
        const float  envLod    = flags.real("env-lod", 1.5f, 0.0f, 8.0f);
        const float  emissive  = flags.real("emissive", 4.2f, 0.0f, 40.0f);
        const float  band      = flags.real("band", 0.16f, 0.005f, 0.5f);
        const float  ior       = flags.real("ior", 1.45f, 1.0f, 2.4f);
        const int    envSize   = flags.integer("env-size", 256);
        bool snapped = false;

        window.OnFrame([&](gldx::win::FrameInfo& info) {
            gldx::win::Window& win = *info.window;
            // Clamp before anything moves: a dragged window or a driver hiccup can
            // hand back a third of a second, and at 40 m/s that teleports the camera
            // clean through the tree line.
            const float dt = static_cast<float>(std::clamp(info.dt, 0.0, 0.1));
            const float aspect = info.fbHeight > 0
                ? static_cast<float>(info.fbWidth) / static_cast<float>(info.fbHeight)
                : 1.0f;

            HandleKeys(win, input, dt);
            const FlyIntent fly = ReadFlyIntent(win, input, dt);

            camera.SetViewportAspect(aspect);
            // --freeze-at S parks the clock S seconds after the loop started. Every
            // moving thing in this scene is a pure function of that clock: the crate
            // samples a pose *table* indexed by it, so two runs of one seed and one
            // second land on the same triangle in the same place. Resolved before
            // the camera moves, because the walking pose comes from the same table
            // mechanism and a screenshot of the camera climbing a ridge has to name
            // one second, not one frame rate.
            const double animTime = freezeAt > 0.0 ? std::min(info.time, freezeAt) : info.time;

            // The demo owns the pose, the camera owns the matrices. Input is written
            // by keyboard and mouse alike, so it is pushed in each frame and read
            // back out afterwards - one position, no second source of truth. With
            // walking on, the path owns the position and the heading and the keyboard
            // does not, which is why the three fly moves below are skipped rather
            // than zeroed: a dt integral left in the pose would make the frame the
            // capture took depend on how many frames came before it.
            if (input.walk) {
                const Pose w = scene.SampleWalk(animTime);
                input.pos = w.pos;
                input.yaw = w.yaw;
            }
            camera.SetYawPitch(input.yaw, input.pitch);
            camera.Translate(input.pos - camera.Position());
            if (!input.walk) {
                camera.MoveForward(fly.forward);
                camera.MoveRight(fly.strafe);
                camera.MoveUp(fly.rise);
            }
            input.pos = camera.Position();
            // Re-asserted every frame because two things move it: the wheel and
            // --fov at startup. The projection is the one piece of camera state the
            // HUD quotes back as a number, so this is also what keeps that number
            // from being a caption rather than a reading.
            camera.SetPerspective(input.fov, aspect, 0.1f, 420.0f);

            scene.UpdateCrate(animTime, input.roam);

            // ---- lighting: one moon and four lanterns ------------------------
            // The engine's LightSetup already packs the std140 block this demo's
            // shaders read, so the only thing Gooch changes is what the fragment
            // stage *does* with it. `sun.direction` is the travel direction, hence
            // the negated moon vector: light has to arrive FROM the disc that is
            // painted in the sky, or the trees are lit from a direction nothing in
            // the frame agrees with.
            gldx::LightSetup setup;
            // The mist's own span, in one place. The panel quotes these numbers back
            // as the distance at which the tree line dissolves, and a second pair of
            // literals for the same thing is how a caption starts lying.
            constexpr float kFogStart = 45.0f, kFogEnd = 190.0f;
            setup.sun.direction = -MoonDirection(field);
            setup.sun.color     = glm::vec3(0.92f, 0.94f, 1.00f);
            // Moonlight, not sunlight. Gooch's band multiplies the albedo by this,
            // and the two flat tones of the band are what the whole frame's contrast
            // budget is spent on: at 1.0 the forest is a daytime scene painted blue,
            // at 0.30 the lit side of a pine sits just under a tenth of linear
            // radiance and the shade - carried by the cold floor rather than by this
            // number - falls a little over half that, which is the step a night
            // sketch needs. The lanterns stay at 2.4 because a fire is meant to
            // outrank the moon by a distance.
            setup.sun.intensity = 0.30f;
            // A cold floor rather than black: everything out of the moon's band and
            // out of a lantern's range still has to have a colour to be.
            setup.ambient = glm::vec3(0.055f, 0.075f, 0.130f);
            if (input.lights) {
                for (const glm::vec3& p : scene.lanterns()) {
                    gldx::PointLight lamp;
                    lamp.position = p;
                    lamp.range    = 13.0f;
                    lamp.color    = glm::vec3(1.00f, 0.60f, 0.26f);
                    lamp.intensity = 2.4f;
                    setup.points.push_back(lamp);
                }
            }
            if (input.fog) {
                // The same tone the shadow tint carries, so distance and shade stop
                // being two different greys and the tree line dissolves instead of
                // ending in a silhouette. These three numbers are also Sky.cpp's
                // kHorizon and kNadir, and they have to stay equal: the ground stops
                // at 200 m with the mist finished at 190, so the sky immediately
                // behind the tree line is the far ground's only possible substitute.
                setup.fogColor = glm::vec3(0.030f, 0.042f, 0.078f);
                setup.fogStart = kFogStart;
                setup.fogEnd   = kFogEnd;
            }
            lights.Update(setup, camera.Position());

            NightPass::Settings& s = nightPass->settings;
            s.scene       = &scene;
            s.sky         = &sky;
            s.skybox      = &skybox;
            s.bandWidth   = band;
            s.capture     = input.capture;
            s.envSize     = envSize;
            s.envMix      = input.envMix;
            s.ior         = ior;
            s.refract     = input.refract ? 1.0f : 0.0f;
            s.envLod      = envLod;
            s.emissive    = emissive;
            s.cull        = input.cull;
            s.wireframe   = input.wireframe;

            hudPass->ior       = ior;
            hudPass->envLod    = envLod;
            hudPass->bandWidth = band;
            hudPass->fogStart  = kFogStart;
            hudPass->fogEnd    = kFogEnd;
            hudPass->animTime  = animTime;
            hudPass->frozenAt  = freezeAt;

            gldx::RenderFrame frame;
            frame.camera = &camera;
            frame.viewProj = camera.ViewProjection();
            frame.post = &post;
            frame.lights = &lights;
            frame.lightSetup = setup;
            frame.useBloom = input.bloom;
            frame.overlay.sprite = &sprite;
            frame.overlay.font = &font;
            // The scene's own 1x1 white, so the overlay does not upload a second one.
            frame.overlay.white = &scene.white();
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
        // scripted sweep asserts on: with capture on the world should report six
        // faces, and with the crate parked it should report five draws and zero.
        std::printf("night_forest: %d world draws / %u instances, %d capture faces,"
                    " env %s\n",
                    nightPass->drawsLastFrame(), nightPass->instancesLastFrame(),
                    nightPass->facesCaptured(), nightPass->envSource());
        return rc;
        // Every GPU-resource owner destructs as runDemo returns, on the render
        // thread and while the window's context is still current.
    };

    return runDemo();
}
