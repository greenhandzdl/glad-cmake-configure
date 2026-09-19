/**
 * @file main.cpp
 * @brief Phase 2 demo: PBR forward renderer with cascaded shadows, image-based
 *        lighting from a procedural HDR sky, and an orbit camera.
 *
 * Controls:
 *   drag mouse   : orbit          scroll      : zoom
 *   A/D or Left/Right : sun azimuth           W/S or Up/Down : sun elevation
 *   1 : toggle cascaded shadows   2 : toggle image-based lighting
 *   Esc : quit
 *
 * All GL work runs on the render thread; the plan's two-phase rule is honoured
 * by building mesh/texture CPU data up front and uploading once at startup.
 */

#include "common.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <utility>
#include <vector>

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "gfx/core/RenderContext.h"
#include "gfx/camera/Camera.h"
#include "gfx/geometry/GeometryFactory.h"
#include "gfx/geometry/Mesh.h"
#include "gfx/light/EnvironmentMap.h"
#include "gfx/light/Light.h"
#include "gfx/light/LightBuffer.h"
#include "gfx/material/Material.h"
#include "gfx/render/Renderer.h"
#include "gfx/render/SkyboxRenderer.h"
#include "gfx/shader/ShaderLib.h"
#include "gfx/shader/ShaderProgram.h"
#include "gfx/shadow/CascadedShadowMap.h"
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
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    in->dragging = (action == GLFW_PRESS);
    double cx = 0, cy = 0;
    glfwGetCursorPos(win, &cx, &cy);
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
};

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

        // ---- scene: PBR test grid (metallic x roughness) + textured cubes ----
        std::vector<SceneObject> objects;

        // Ground plane first (drawn early, never casts).
        {
            SceneObject o;
            UploadInto(o.mesh, gfx::GeometryFactory::Plane(1.0f));
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
                UploadInto(o.mesh, gfx::GeometryFactory::Sphere(0.5f, 48, 32));
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
            UploadInto(o.mesh, gfx::GeometryFactory::Cube(1.0f));
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

        while (!glfwWindowShouldClose(window)) {
            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);
            static bool shadowToggleArmed = true;
            static bool iblToggleArmed = true;
            const bool k1 = glfwGetKey(window, GLFW_KEY_1) == GLFW_PRESS;
            const bool k2 = glfwGetKey(window, GLFW_KEY_2) == GLFW_PRESS;
            if (k1 && shadowToggleArmed) { input.useShadow = !input.useShadow; shadowToggleArmed = false; }
            else if (!k1) shadowToggleArmed = true;
            if (k2 && iblToggleArmed) { input.useIbl = !input.useIbl; iblToggleArmed = false; }
            else if (!k2) iblToggleArmed = true;
            HandleKeys(window, input);

            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(window, &fbw, &fbh);
            camera.SetViewportAspect(fbh > 0 ? static_cast<float>(fbw) / fbh : 1.0f);

            const float cp = std::cos(input.pitch);
            const glm::vec3 eye(
                target.x + input.radius * cp * std::sin(input.yaw),
                target.y + input.radius * std::sin(input.pitch),
                target.z + input.radius * cp * std::cos(input.yaw));
            camera.LookAt(eye, target, glm::vec3(0, 1, 0));

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

            // --- main PBR pass ---
            renderer.BeginFrame(fbw, fbh);

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

            for (const auto& o : objects) {
                pbr->Set("uModel", o.model);
                pbr->Set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(o.model))));
                o.material.Apply(*pbr);
                o.mesh.Draw();
            }

            // --- skybox background (drawn last with the LEQUAL-depth trick) ---
            skybox.Draw(camera.ViewProjection(), env.sky(), gfx::texunit::skybox);

            renderer.EndFrame();
            glfwSwapBuffers(window);
            glfwPollEvents();
        }
        // Every GPU-resource owner destructs here, on the render thread.
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
