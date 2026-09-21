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
 * can switch each one without a keyboard: see demo_cli.h. --yaw / --pitch /
 * --radius aim the orbit camera, which a sweep needs to bring the instanced
 * field and the sky into the frame at all; --freeze-at SECONDS parks the
 * animation clock, which is what makes two runs of one setting pixel-identical.
 *
 * The application owns the GL resources (meshes / materials / textures / passes);
 * scene nodes reference them by non-owning pointer, so the whole frame's draw
 * sequence - once a wall of inline gl* calls here - is now a single
 * renderer.Render(frame). All GL still happens on the render thread.
 */

// Platform.h is deliberately a plain text include (not part of module gldx):
// it pulls <glad/gl.h> before <GLFW/glfw3.h> and defines the app/window
// constants + GLFW_PLATFORM_* macros this executable's #if checks rely on.
#include "gldx/core/Platform.h"

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "demo/demo_cli.h"

// The whole engine as a single C++20 named module: no per-header includes.
import gldx;

namespace {

// ---- orbit-camera + interaction state (shared with the GLFW callbacks) ------
struct Input {
    float yaw   = 0.65f;    // azimuth around the target (rad)
    float pitch = 0.42f;    // elevation above the target (rad)
    float radius = 11.0f;   // distance to the target
    double lastX = 0.0;
    double lastY = 0.0;
    bool  dragging = false;
    float sunAzimuth = 0.7f;
    float sunElevation = 0.85f;   // radians above the horizon
    bool  useShadow = true;
    bool  useIbl = true;
    bool  useBloom = true;
    bool  useDebug = false;
    bool  useInstances = false;
    bool  useSky = true;     // background cube; off leaves the clear colour
    bool  ortho = false;   // current projection: false=perspective, true=ortho
    // Pending right-click pick request (consumed + cleared in the main loop).
    // Stored normalised to the window, not raw cursor pixels: the pick ray is
    // built in framebuffer pixels, which differ by the content scale (2 on
    // Retina) from glfwGetCursorPos' window coordinates.
    bool  pickPending = false;
    float pickX = 0.0f, pickY = 0.0f;   // [0..1] across the window
};

// A few well-known system fonts, tried in order; HUD text just no-ops if none
// are found, so a missing font never breaks the render.
const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

// Unit vector pointing from the scene toward the sun (elevation > 0 => upward).
glm::vec3 SunToward(const Input& in) {
    const float ce = std::cos(in.sunElevation);
    return glm::normalize(glm::vec3(ce * std::sin(in.sunAzimuth),
                                    std::sin(in.sunElevation),
                                    ce * std::cos(in.sunAzimuth)));
}

void MouseCallback(GLFWwindow* win, double x, double y) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in || !in->dragging) return;
    const float dx = static_cast<float>(x - in->lastX);
    const float dy = static_cast<float>(y - in->lastY);
    in->yaw   -= dx * 0.006f;
    in->pitch += dy * 0.006f;
    in->pitch = std::clamp(in->pitch, -1.45f, 1.45f);
    in->lastX = x;
    in->lastY = y;
}

void MouseButtonCallback(GLFWwindow* win, int button, int action, int) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    double cx = 0, cy = 0;
    glfwGetCursorPos(win, &cx, &cy);
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {           // request a pick at this pixel
            int ww = 0, wh = 0;
            glfwGetWindowSize(win, &ww, &wh);
            if (ww > 0 && wh > 0) {
                in->pickPending = true;
                in->pickX = static_cast<float>(cx / ww);   // window-normalised
                in->pickY = static_cast<float>(cy / wh);
            }
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    in->dragging = (action == GLFW_PRESS);
    in->lastX = cx;
    in->lastY = cy;
}

void ScrollCallback(GLFWwindow* win, double, double dy) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    in->radius = std::clamp(in->radius - static_cast<float>(dy) * 0.6f, 2.0f, 60.0f);
}

void HandleKeys(GLFWwindow* win, Input& in) {
    const float step = 0.04f;
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS)
        in.sunAzimuth -= step;
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS)
        in.sunAzimuth += step;
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS)
        in.sunElevation = std::min(in.sunElevation + step, 1.5f);
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS)
        in.sunElevation = std::max(in.sunElevation - step, 0.05f);
}

// Owns the GPU mesh + CPU material for one scene object. Scene nodes point here;
// storing them by unique_ptr keeps the addresses stable across the whole run.
struct Owned {
    gldx::Mesh        mesh;
    gldx::PbrMaterial material;
};

// CPU-side checker albedo (Stage A, no GL).
gldx::Texture2DDesc MakeCheckerDesc(int size = 512, int cells = 8) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 3;
    d.srgb = true;
    d.pixels.resize(static_cast<std::size_t>(size) * size * 3);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool on = ((x / cell) + (y / cell)) % 2 == 0;
            const glm::vec3 c = on ? glm::vec3(0.82f, 0.80f, 0.76f) : glm::vec3(0.16f, 0.18f, 0.22f);
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            d.pixels[i + 0] = static_cast<std::uint8_t>(c.r * 255.0f);
            d.pixels[i + 1] = static_cast<std::uint8_t>(c.g * 255.0f);
            d.pixels[i + 2] = static_cast<std::uint8_t>(c.b * 255.0f);
        }
    }
    return d;
}

// 1x1 opaque white texel: a cheap solid fill for HUD panels via SpriteBatch.
gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1;
    d.channels = 4;
    d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

} // namespace

int main(int argc, char** argv) {
    const demo::Flags flags(argc, argv,
                            {"shadow", "ibl", "bloom", "debug", "instances", "sky", "ortho"},
                            {"freeze-at", "yaw", "pitch", "radius"}, "pbr_showcase");
    if (flags.wantsHelp()) {
        flags.printUsage();
        return 0;
    }

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if GLFW_PLATFORM_MACOS
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(gldx::kWindowWidth, gldx::kWindowHeight, gldx::kWindowTitle, nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window (OpenGL 4.1 core?)\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    gldx::RenderContext::MarkAsRenderThread();

    std::printf("%s %s\n", gldx::kAppName, gldx::kAppVersion);
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
    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, MouseCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetScrollCallback(window, ScrollCallback);

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

        gldx::Texture2D checker;
        checker.Upload(MakeCheckerDesc());

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
        for (const char* candidate : kFontCandidates) {
            if (font.LoadFromFile(candidate, 48.0f)) break;
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
        gldx::InstancedMesh instField;   // CPU data built (Stage A), uploaded once (Stage B)
        {
            gldx::MeshData geo = gldx::GeometryFactory::Cube(1.0f);
            std::vector<gldx::Instance> insts;
            constexpr int n = 8;
            for (int ix = 0; ix < n; ++ix) {
                for (int iz = 0; iz < n; ++iz) {
                    const float x = 7.5f + ix * 1.4f;
                    const float z = -4.9f + iz * 1.4f;
                    const float h = 0.5f + 0.5f * static_cast<float>((ix * 3 + iz * 5) % 6);
                    const glm::mat4 m =
                        glm::translate(glm::mat4(1.0f), glm::vec3(x, h * 0.5f, z)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, h, 0.4f));
                    gldx::Instance in;
                    in.model = m;
                    const float t = static_cast<float>((ix + iz) % 5) / 4.0f;
                    in.color = glm::vec4(0.3f + 0.6f * t, 0.4f, 0.85f - 0.5f * t, 1.0f);
                    insts.push_back(in);
                }
            }
            instField.Create(std::move(geo), std::move(insts));
        }

        // ---- scene graph -----------------------------------------------------
        // Objects are owned by `owned` (stable addresses); nodes reference them.
        std::vector<std::unique_ptr<Owned>> owned;
        gldx::Scene scene;
        int nextId = 0;

        auto addObject = [&](gldx::MeshData data, const gldx::PbrMaterial& matCfg,
                             const gldx::Transform& xf,
                             gldx::SceneNode* parent = nullptr) -> gldx::SceneNode& {
            auto o = std::make_unique<Owned>();
            o->material = matCfg;
            if (!o->material.placeholder) o->material.placeholder = &checker;
            glm::vec3 c;
            float r;
            gldx::SceneNode::BoundsFromMeshData(data, c, r);
            o->mesh.Upload(std::move(data));
            Owned* raw = o.get();
            owned.push_back(std::move(o));
            gldx::SceneNode& node = parent ? parent->AddChild(xf) : scene.CreateRoot(xf);
            node.SetRenderable(&raw->mesh, &raw->material);
            node.SetLocalBounds(c, r);
            node.id = nextId++;
            return node;
        };

        // Ground plane (large scale, never casts).
        {
            gldx::Transform t;
            t.scale = glm::vec3(24.0f, 1.0f, 24.0f);
            gldx::PbrMaterial m;
            m.baseColor = glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
            m.roughness = 0.85f;
            m.albedo = &checker;
            addObject(gldx::GeometryFactory::Plane(1.0f), m, t).castsShadow = false;
        }

        // PBR test grid (metallic x roughness).
        for (int i = 0; i < 5; ++i) {
            for (int j = 0; j < 5; ++j) {
                gldx::Transform t;
                t.translation = glm::vec3(-3.0f + i * 1.5f, 0.5f, -3.0f + j * 1.5f);
                gldx::PbrMaterial m;
                m.metallic  = static_cast<float>(i) / 4.0f;
                m.roughness = 0.05f + 0.9f * static_cast<float>(j) / 4.0f;
                m.baseColor = glm::vec4(0.9f, 0.5f, 0.25f, 1.0f);
                addObject(gldx::GeometryFactory::Sphere(0.5f, 48, 32), m, t);
            }
        }

        // Textured cubes (checker albedo).
        for (int k = 0; k < 3; ++k) {
            gldx::Transform t;
            t.translation = glm::vec3(-1.6f + k * 1.6f, 0.5f, 3.6f);
            t.SetAxisAngle(glm::vec3(0, 1, 0), 0.5f * k);
            gldx::PbrMaterial m;
            m.baseColor = glm::vec4(1.0f);
            m.roughness = 0.45f;
            m.albedo = &checker;
            addObject(gldx::GeometryFactory::Cube(1.0f), m, t);
        }

        // Hierarchy demo: an empty pivot carrying orbiting children. The pivot is
        // spun every frame; the children ride along via world-matrix propagation.
        gldx::SceneNode* carousel = &scene.CreateRoot();
        for (int c = 0; c < 3; ++c) {
            const float a = c * 2.0f * 3.14159265f / 3.0f;
            gldx::Transform t;
            t.translation = glm::vec3(std::cos(a) * 1.2f, 1.2f, std::sin(a) * 1.2f);
            gldx::PbrMaterial m;
            m.metallic = 0.9f;
            m.roughness = 0.2f;
            m.baseColor = glm::vec4(0.2f + 0.4f * c, 0.6f, 0.9f - 0.3f * c, 1.0f);
            addObject(gldx::GeometryFactory::Cube(0.5f), m, t, carousel);
        }

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
        const double startedAt = glfwGetTime();
        const double quitAfter = flags.quitAfter();
        const double freezeAt = flags.number("freeze-at");
        double smoothedFps = 60.0;
        int fpsFrames = 0;
        double fpsWindowStart = startedAt;

        while (!glfwWindowShouldClose(window)) {
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);
            // Edge-detect the 1..6 toggles: flip once per press, rearm on release.
            auto edgeToggle = [](bool pressed, bool& armed, bool& flag) {
                if (pressed && armed) { flag = !flag; armed = false; }
                else if (!pressed) armed = true;
            };
            static bool shadowArmed = true, iblArmed = true, bloomArmed = true,
                        debugArmed = true, instArmed = true, skyArmed = true, projArmed = true;
            edgeToggle(glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS, shadowArmed, input.useShadow);
            edgeToggle(glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS, iblArmed,    input.useIbl);
            edgeToggle(glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS, bloomArmed,  input.useBloom);
            edgeToggle(glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS, debugArmed,  input.useDebug);
            edgeToggle(glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS, instArmed,   input.useInstances);
            edgeToggle(glfwGetKey(window, GLFW_KEY_6) == GLFW_PRESS, skyArmed,    input.useSky);
            // Tab swaps perspective <-> orthographic once per press.
            if (bool tabDown = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS; tabDown && projArmed) {
                input.ortho = camera.ToggleProjection()
                              == gldx::Camera::Projection::Orthographic;
                projArmed = false;
            } else if (!tabDown) {
                projArmed = true;
            }
            HandleKeys(window, input);

            // FPS for the HUD, averaged over a fixed wall-clock window rather
            // than an EMA of 1/dt: the EMA only echoed whichever frame last
            // stalled, so the readout never matched the frame it was drawn on.
            const double now = glfwGetTime();
            ++fpsFrames;
            if (const double span = now - fpsWindowStart; span >= 0.5) {
                smoothedFps = static_cast<double>(fpsFrames) / span;
                fpsFrames = 0;
                fpsWindowStart = now;
            }
            if (quitAfter > 0.0 && now - startedAt >= quitAfter)
                glfwSetWindowShouldClose(window, true);

            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(window, &fbw, &fbh);
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
            // timer: glfwGetTime() counts from machine boot, so an absolute clock
            // left the parked phase different on every run (and lost precision on
            // a long-lived desktop, where the float cast alone moved the angle by
            // thousandths of a radian per frame).
            const double animTime =
                freezeAt > 0.0 ? startedAt + std::min(now - startedAt, freezeAt) : now;
            carousel->local().SetAxisAngle(glm::vec3(0, 1, 0),
                                           static_cast<float>(animTime - startedAt) * 0.6f);
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
            frame.smoothedFps = smoothedFps;

            profiler.BeginFrame();
            renderer.Render(frame);

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
        return 0;   // every GPU-resource owner destructs here, on the render thread.
    };

    const int rc = runDemo();   // destructors run while the context is still current
    glfwDestroyWindow(window);
    glfwTerminate();
    return rc;
}
