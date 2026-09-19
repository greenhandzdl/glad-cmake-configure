/**
 * @file main.cpp
 * @brief Phase 4 demo: the Phase 3 HDR/PBR/CSM/IBL renderer plus frustum
 *        culling, a DebugDraw line overlay, CPU+GPU profiling, mouse picking,
 *        and a GPU-instanced field, surfaced through the sprite-batched text HUD.
 *
 * Controls:
 *   drag mouse   : orbit          scroll      : zoom
 *   A/D or Left/Right : sun azimuth           W/S or Up/Down : sun elevation
 *   right click  : pick an object (bounding-sphere ray test)
 *   1 : cascaded shadows   2 : IBL   3 : bloom   4 : debug lines   5 : instanced field
 *   Esc : quit
 *
 * All GL work runs on the render thread; the plan's two-phase rule is honoured
 * by building mesh/texture CPU data up front and uploading once at startup.
 */

#include "common.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "gfx/core/RenderContext.h"
#include "gfx/camera/Camera.h"
#include "gfx/camera/Frustum.h"
#include "gfx/camera/Picking.h"
#include "gfx/debug/DebugDraw.h"
#include "gfx/debug/Profiler.h"
#include "gfx/geometry/GeometryFactory.h"
#include "gfx/geometry/InstancedMesh.h"
#include "gfx/geometry/Mesh.h"
#include "gfx/light/EnvironmentMap.h"
#include "gfx/light/Light.h"
#include "gfx/light/LightBuffer.h"
#include "gfx/material/Material.h"
#include "gfx/render/PostProcessChain.h"
#include "gfx/render/Renderer.h"
#include "gfx/render/SkyboxRenderer.h"
#include "gfx/render/SpriteBatch.h"
#include "gfx/shader/ShaderLib.h"
#include "gfx/shader/ShaderProgram.h"
#include "gfx/shadow/CascadedShadowMap.h"
#include "gfx/text/Font.h"
#include "gfx/text/TextRenderer.h"
#include "gfx/texture/Texture2D.h"

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
    int   selected = -1;         // picked object index, -1 = none
    // Pending right-click pick request (consumed + cleared in the main loop).
    bool  pickPending = false;
    float pickX = 0.0f, pickY = 0.0f;
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
            in->pickPending = true;
            in->pickX = static_cast<float>(cx);
            in->pickY = static_cast<float>(cy);
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

// ---- one draw-call record (Mesh is move-only, so SceneObject is too) --------
struct SceneObject {
    gfx::Mesh        mesh;
    glm::mat4        model{1.0f};
    gfx::PbrMaterial material;
    bool             castsShadow = true;
    // Local-space bounding sphere (from the source MeshData), used for frustum
    // culling and picking once transformed into world space by `model`.
    glm::vec3        boundCenter{0.0f};
    float            boundRadius = 0.5f;
};

// Axis-aligned bounds of CPU geometry -> a local bounding sphere.
struct Sphere { glm::vec3 center{0.0f}; float radius = 0.5f; };

Sphere ComputeBounds(const gfx::MeshData& data) {
    Sphere s;
    if (data.vertices.empty()) return s;
    glm::vec3 mn(data.vertices[0].position), mx = mn;
    for (const auto& v : data.vertices) {
        mn = glm::min(mn, v.position);
        mx = glm::max(mx, v.position);
    }
    s.center = (mn + mx) * 0.5f;
    s.radius = glm::length(mx - s.center);
    return s;
}

// Largest axis scale of a model matrix (approximates sphere -> world growth).
float MaxScale(const glm::mat4& m) {
    auto len = [&m](int c) { return glm::length(glm::vec3(m[c])); };
    return std::max({len(0), len(1), len(2)});
}

// CPU-side checker albedo (Stage A, no GL).
gfx::Texture2DDesc MakeCheckerDesc(int size = 512, int cells = 8) {
    gfx::Texture2DDesc d;
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

// Upload a fresh mesh from CPU geometry data (render thread) into `slot`.
void UploadInto(gfx::Mesh& slot, gfx::MeshData data) {
    gfx::Mesh m;
    m.Upload(std::move(data));
    slot = std::move(m);
}

// Upload geometry into a scene object and cache its local bounding sphere.
void AttachMesh(SceneObject& o, gfx::MeshData data) {
    const Sphere s = ComputeBounds(data);
    o.boundCenter = s.center;
    o.boundRadius = s.radius;
    UploadInto(o.mesh, std::move(data));
}

// 1x1 opaque white texel: a cheap solid fill for HUD panels via SpriteBatch.
gfx::Texture2DDesc MakeSolidDesc() {
    gfx::Texture2DDesc d;
    d.width = d.height = 1;
    d.channels = 4;
    d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

} // namespace

int main(int /*argc*/, char** /*argv*/) {
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

    GLFWwindow* window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT, WINDOW_TITLE, nullptr, nullptr);
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

    gfx::RenderContext::MarkAsRenderThread();

    std::printf("%s %s\n", APP_NAME, APP_VERSION);
    std::printf("OpenGL %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    Input input;
    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, MouseCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetScrollCallback(window, ScrollCallback);

    // All GPU-resource owners live in this scope so destructors run while the
    // context is still current (before the window is destroyed).
    {
        gfx::Renderer renderer;
        renderer.Init();
        renderer.SetClearColor(glm::vec4(0.02f, 0.02f, 0.03f, 1.0f));

        auto pbr = gfx::ShaderProgram::CreateFromSource(gfx::shaders::kPbrVertex, gfx::shaders::kPbrFragment);
        if (!pbr) {
            std::fprintf(stderr, "PBR shader error:\n%s\n", pbr.error().c_str());
            glfwDestroyWindow(window); glfwTerminate(); return 1;
        }
        auto depth = gfx::ShaderProgram::CreateFromSource(gfx::shaders::kDepthVertex, gfx::shaders::kDepthFragment);
        if (!depth) {
            std::fprintf(stderr, "Depth shader error:\n%s\n", depth.error().c_str());
            glfwDestroyWindow(window); glfwTerminate(); return 1;
        }

        gfx::SkyboxRenderer skybox;
        if (!skybox.Init()) {
            std::fprintf(stderr, "Skybox init failed\n");
            glfwDestroyWindow(window); glfwTerminate(); return 1;
        }

        // Procedural HDR sky + IBL precomputes, baked once from the initial sun.
        const glm::vec3 initialTravel = -SunToward(input);
        gfx::EnvironmentMap env;
        if (!env.Generate(initialTravel, 256, 32, 256)) {
            std::fprintf(stderr, "Environment generation failed\n");
            glfwDestroyWindow(window); glfwTerminate(); return 1;
        }

        gfx::Texture2D checker;
        checker.Upload(MakeCheckerDesc());

        // ---- Phase 3: post-process chain + 2D HUD infrastructure ------------
        gfx::PostProcessChain post;
        if (!post.Init()) {
            std::fprintf(stderr, "PostProcessChain init failed\n");
            glfwDestroyWindow(window); glfwTerminate(); return 1;
        }
        post.SetExposure(1.1f);

        gfx::SpriteBatch sprite;
        if (!sprite.Init()) {
            std::fprintf(stderr, "SpriteBatch init failed\n");
            glfwDestroyWindow(window); glfwTerminate(); return 1;
        }

        gfx::Font font;
        for (const char* candidate : kFontCandidates) {
            if (font.LoadFromFile(candidate, 48.0f)) break;
        }
        if (!font.loaded()) std::fprintf(stderr, "HUD font not found; text overlay disabled\n");

        gfx::Texture2D white;
        white.Upload(MakeSolidDesc());

        // ---- scene: PBR test grid (metallic x roughness) + textured cubes ----
        std::vector<SceneObject> objects;

        // Ground plane first (drawn early, never casts).
        {
            SceneObject o;
            AttachMesh(o, gfx::GeometryFactory::Plane(1.0f));
            o.model = glm::scale(glm::mat4(1.0f), glm::vec3(24.0f, 1.0f, 24.0f));
            o.material.baseColor = glm::vec4(0.9f, 0.9f, 0.92f, 1.0f);
            o.material.roughness = 0.85f;
            o.material.albedo = &checker;
            o.castsShadow = false;
            objects.push_back(std::move(o));
        }

        for (int i = 0; i < 5; ++i) {
            for (int j = 0; j < 5; ++j) {
                SceneObject o;
                AttachMesh(o, gfx::GeometryFactory::Sphere(0.5f, 48, 32));
                const glm::vec3 pos(-3.0f + i * 1.5f, 0.5f, -3.0f + j * 1.5f);
                o.model = glm::translate(glm::mat4(1.0f), pos);
                o.material.metallic = static_cast<float>(i) / 4.0f;
                o.material.roughness = 0.05f + 0.9f * static_cast<float>(j) / 4.0f;
                o.material.baseColor = glm::vec4(0.9f, 0.5f, 0.25f, 1.0f);
                objects.push_back(std::move(o));
            }
        }

        for (int k = 0; k < 3; ++k) {
            SceneObject o;
            AttachMesh(o, gfx::GeometryFactory::Cube(1.0f));
            const glm::vec3 pos(-1.6f + k * 1.6f, 0.5f, 3.6f);
            o.model = glm::translate(glm::mat4(1.0f), pos) *
                      glm::rotate(glm::mat4(1.0f), 0.5f * k, glm::vec3(0, 1, 0));
            o.material.baseColor = glm::vec4(1.0f);
            o.material.roughness = 0.45f;
            o.material.albedo = &checker;
            objects.push_back(std::move(o));
        }

        // Every material samples its four maps from fixed units; give each one a
        // valid 2D placeholder so units left without a real map still match the
        // sampler type (see PbrMaterial::placeholder).
        for (auto& o : objects) o.material.placeholder = &checker;

        gfx::LightBuffer lightBuffer;
        lightBuffer.Init();
        gfx::CascadedShadowMap csm;
        csm.Init(2048);

        gfx::Camera camera;
        camera.SetPerspective(45.0f, 1.0f, 0.1f, 200.0f);

        const glm::vec3 target(0.0f, 0.6f, 0.0f);

        // Sampler-unit uniforms persist on the program; set them once.
        pbr->Use();
        pbr->Set("uShadowMap", static_cast<int>(gfx::texunit::shadowArray));
        pbr->Set("uIrradiance", static_cast<int>(gfx::texunit::irradiance));
        pbr->Set("uPrefilter", static_cast<int>(gfx::texunit::prefilter));
        pbr->Set("uBrdfLut", static_cast<int>(gfx::texunit::brdfLut));
        // GLSL 4.10: map the std140 blocks onto the fixed UBO binding points
        // that LightBuffer / CascadedShadowMap bind their buffers to each frame.
        pbr->SetBlockBinding("LightingBlock", gfx::LightBuffer::kBinding);
        pbr->SetBlockBinding("ShadowBlock", gfx::CascadedShadowMap::kShadowBinding);

        // ---- Phase 4: debug overlay, profiler, GPU-instanced field -----------
        gfx::DebugDraw debug;
        if (!debug.Init()) std::fprintf(stderr, "DebugDraw init failed\n");

        gfx::Profiler profiler;
        profiler.Init();

        auto instProg = gfx::ShaderProgram::CreateFromSource(
            gfx::shaders::kInstancedVertex, gfx::shaders::kInstancedFragment);
        if (!instProg) {
            std::fprintf(stderr, "Instanced shader error:\n%s\n", instProg.error().c_str());
        } else {
            instProg->Use();
            instProg->SetBlockBinding("LightingBlock", gfx::LightBuffer::kBinding);
        }
        gfx::InstancedMesh instField;   // CPU data built (Stage A), uploaded once (Stage B)
        {
            gfx::MeshData geo = gfx::GeometryFactory::Cube(1.0f);
            std::vector<gfx::Instance> insts;
            constexpr int n = 8;
            for (int ix = 0; ix < n; ++ix) {
                for (int iz = 0; iz < n; ++iz) {
                    const float x = 7.5f + ix * 1.4f;
                    const float z = -4.9f + iz * 1.4f;
                    const float h = 0.5f + 0.5f * static_cast<float>((ix * 3 + iz * 5) % 6);
                    const glm::mat4 m =
                        glm::translate(glm::mat4(1.0f), glm::vec3(x, h * 0.5f, z)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, h, 0.4f));
                    gfx::Instance in;
                    in.model = m;
                    const float t = static_cast<float>((ix + iz) % 5) / 4.0f;
                    in.color = glm::vec4(0.3f + 0.6f * t, 0.4f, 0.85f - 0.5f * t, 1.0f);
                    insts.push_back(in);
                }
            }
            instField.Create(std::move(geo), std::move(insts));
        }

        double lastFrameTime = glfwGetTime();
        while (!glfwWindowShouldClose(window)) {
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);
            static bool shadowToggleArmed = true;
            static bool iblToggleArmed = true;
            static bool bloomToggleArmed = true;
            static bool debugToggleArmed = true;
            static bool instToggleArmed = true;
            const bool k1 = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
            const bool k2 = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
            const bool k3 = glfwGetKey(window, GLFW_KEY_3) == GLFW_PRESS;
            const bool k4 = glfwGetKey(window, GLFW_KEY_4) == GLFW_PRESS;
            const bool k5 = glfwGetKey(window, GLFW_KEY_5) == GLFW_PRESS;
            if (k1 && shadowToggleArmed) { input.useShadow = !input.useShadow; shadowToggleArmed = false; }
            else if (!k1) shadowToggleArmed = true;
            if (k2 && iblToggleArmed) { input.useIbl = !input.useIbl; iblToggleArmed = false; }
            else if (!k2) iblToggleArmed = true;
            if (k3 && bloomToggleArmed) { input.useBloom = !input.useBloom; bloomToggleArmed = false; }
            else if (!k3) bloomToggleArmed = true;
            if (k4 && debugToggleArmed) { input.useDebug = !input.useDebug; debugToggleArmed = false; }
            else if (!k4) debugToggleArmed = true;
            if (k5 && instToggleArmed) { input.useInstances = !input.useInstances; instToggleArmed = false; }
            else if (!k5) instToggleArmed = true;
            HandleKeys(window, input);
            profiler.BeginFrame();

            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(window, &fbw, &fbh);
            camera.SetViewportAspect(fbh > 0 ? static_cast<float>(fbw) / fbh : 1.0f);

            const float cp = std::cos(input.pitch);
            const glm::vec3 eye(
                target.x + input.radius * cp * std::sin(input.yaw),
                target.y + input.radius * std::sin(input.pitch),
                target.z + input.radius * cp * std::cos(input.yaw));
            camera.LookAt(eye, target, glm::vec3(0, 1, 0));

            // World-space bounding spheres (culling + picking) and the frustum.
            const glm::mat4 viewProj = camera.ViewProjection();
            gfx::Frustum frustum;
            frustum.Extract(viewProj);
            std::vector<std::pair<glm::vec3, float>> spheres;
            spheres.reserve(objects.size());
            for (const auto& o : objects) {
                const glm::vec3 wc = glm::vec3(o.model * glm::vec4(o.boundCenter, 1.0f));
                spheres.emplace_back(wc, o.boundRadius * MaxScale(o.model));
            }

            // Consume a pending right-click pick (bounding-sphere ray test).
            if (input.pickPending) {
                input.pickPending = false;
                const gfx::Ray ray = gfx::PickRay(input.pickX, input.pickY, fbw, fbh,
                                                  camera.InverseViewProjection(), camera.Position());
                input.selected = gfx::PickNearest(ray, spheres);
            }

            const glm::vec3 towardSun = SunToward(input);
            const glm::vec3 sunTravel = -towardSun;

            gfx::LightSetup setup;
            setup.sun.direction = sunTravel;
            setup.sun.color = glm::vec3(1.0f);
            setup.sun.intensity = 3.0f;
            setup.ambient = input.useIbl ? glm::vec3(0.0f) : glm::vec3(0.05f);
            lightBuffer.Update(setup, camera.Position());

            // --- shadow depth pass ---
            if (input.useShadow) {
                csm.Update(camera, towardSun);
                depth->Use();
                for (int i = 0; i < gfx::kCascadeCount; ++i) {
                    csm.BeginCascade(i);
                    depth->Set("uLightMat", csm.data().lightMat[i]);
                    for (const auto& o : objects) {
                        if (!o.castsShadow) continue;
                        depth->Set("uModel", o.model);
                        o.mesh.Draw();
                    }
                    csm.EndCascade();
                }
                csm.Upload();
            }

            // --- main PBR pass: render linear HDR into the (MSAA) scene target ---
            post.Resize(fbw, fbh, 4);
            post.BeginScene();

            lightBuffer.Bind();
            csm.BindUniform();

            pbr->Use();
            pbr->Set("uViewProj", camera.ViewProjection());
            pbr->Set("uUseShadow", input.useShadow ? 1 : 0);
            pbr->Set("uUseIbl", input.useIbl ? 1 : 0);

            csm.Bind(gfx::texunit::shadowArray);
            unsigned unit = gfx::texunit::irradiance;
            env.BindIrradiance(unit);
            env.BindPrefilter(unit);
            env.BindBrdf(unit);

            int visibleCount = 0;
            for (std::size_t i = 0; i < objects.size(); ++i) {
                const auto& o = objects[i];
                if (!frustum.SphereVisible(spheres[i].first, spheres[i].second)) continue;
                ++visibleCount;
                pbr->Set("uModel", o.model);
                pbr->Set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(o.model))));
                if (static_cast<int>(i) == input.selected) {
                    gfx::PbrMaterial hl = o.material;              // copy: highlight is per-frame
                    hl.baseColor = glm::vec4(1.0f, 0.85f, 0.2f, 1.0f);
                    hl.metallic = 0.1f;
                    hl.roughness = 0.25f;
                    hl.Apply(*pbr);
                } else {
                    o.material.Apply(*pbr);
                }
                o.mesh.Draw();
            }

            // --- GPU-instanced field (lit by the same LightingBlock UBO) ---
            if (input.useInstances && instProg && instField.valid()) {
                instProg->Use();
                instProg->Set("uViewProj", viewProj);
                instField.Draw();
            }

            // --- skybox background (drawn last with the LEQUAL-depth trick) ---
            skybox.Draw(camera.ViewProjection(), env.sky(), gfx::texunit::skybox);

            // --- post-process: MSAA resolve -> bloom -> ACES composite to screen ---
            post.EndScene();
            if (input.useBloom) {
                post.RenderBloom();
                post.SetBloomStrength(0.45f);
            } else {
                post.SetBloomStrength(0.0f);
            }
            post.Composite();

            // --- DebugDraw line overlay (world space, on the tone-mapped screen) ---
            if (input.useDebug || input.selected >= 0) {
                debug.Clear();
                if (input.useDebug) {
                    debug.PushAxes(glm::vec3(0.0f), 2.0f);
                    for (std::size_t i = 0; i < objects.size(); ++i) {
                        const auto& [c, r] = spheres[i];
                        const bool sel = (static_cast<int>(i) == input.selected);
                        debug.PushBoxCenter(c, glm::vec3(r),
                                            sel ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f)
                                                : glm::vec4(0.2f, 0.9f, 0.4f, 0.6f));
                    }
                } else if (input.selected >= 0) {
                    const auto& [c, r] = spheres[input.selected];
                    debug.PushBoxCenter(c, glm::vec3(r), glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
                }
                debug.Draw(viewProj);
            }

            // --- HUD (sprites/text) drawn on the tone-mapped default framebuffer ---
            const double now = glfwGetTime();
            static double smoothedFps = 60.0;
            const double dt = now - lastFrameTime;
            lastFrameTime = now;
            if (dt > 0.0) smoothedFps += (1.0 / dt - smoothedFps) * 0.1;

            sprite.Begin(white, fbw, fbh);
            sprite.Draw(white, 0.0f, 0.0f, 470.0f, 118.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                        glm::vec4(0.0f, 0.0f, 0.0f, 0.35f));   // translucent panel
            char line[220];
            std::snprintf(line, sizeof(line),
                          "GLFW_Template - Phase 4: cull/instance/pick   %.0f fps", smoothedFps);
            gfx::TextRenderer::Draw(sprite, font, line, 12.0f, 8.0f, 22.0f, glm::vec4(1.0f));
            std::snprintf(line, sizeof(line),
                          "CPU %.2f ms   GPU %.2f ms   visible %d/%d   sel %d",
                          profiler.CpuMs(), profiler.GpuMs(), visibleCount,
                          static_cast<int>(objects.size()), input.selected);
            gfx::TextRenderer::Draw(sprite, font, line, 12.0f, 34.0f, 20.0f,
                                    glm::vec4(0.75f, 0.85f, 1.0f, 1.0f));
            std::snprintf(line, sizeof(line),
                          "Shadow %s  IBL %s  Bloom %s  Debug %s  Inst %s",
                          input.useShadow ? "ON" : "OFF",
                          input.useIbl ? "ON" : "OFF",
                          input.useBloom ? "ON" : "OFF",
                          input.useDebug ? "ON" : "OFF",
                          input.useInstances ? "ON" : "OFF");
            gfx::TextRenderer::Draw(sprite, font, line, 12.0f, 60.0f, 20.0f,
                                    glm::vec4(0.75f, 0.85f, 1.0f, 1.0f));
            gfx::TextRenderer::Draw(sprite, font,
                                    "drag=orbit scroll=zoom A/D W/S=sun  1=shd 2=ibl 3=blm 4=dbg 5=inst  rclick=pick",
                                    12.0f, 86.0f, 17.0f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
            sprite.End();

            profiler.EndFrame();

            glfwSwapBuffers(window);
            glfwPollEvents();
        }
        // Every GPU-resource owner destructs here, on the render thread.
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
