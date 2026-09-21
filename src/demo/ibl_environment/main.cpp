/**
 * @file main.cpp
 * @brief ibl_environment - image-based lighting from a procedurally baked HDR sky.
 *
 * EnvironmentMap::Generate bakes four GPU maps on the render thread from a
 * procedural sun: the HDR sky cube (also the background), a cosine-convolved
 * irradiance cube, a GGX-prefiltered specular cube and a BRDF LUT. Handing the
 * frame the `sky.env` record and setting `ibl.enabled` makes GeometryPass bind
 * those three samplers and drive uUseIbl, so the spheres are lit *by the sky*
 * rather than by a flat ambient term - the metal balls mirror the horizon, the
 * rough ones pick up a soft sky-blue diffuse. A dim directional sun is kept for
 * a readable highlight; `ibl` toggles the image-based contribution off to show
 * the difference (ambient falls back to the UBO's flat tint).
 *
 * Controls: 2 toggles IBL, Esc quits, drag orbits, --on/--off ibl,
 * --quit-after SECONDS for headless.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

struct View {
    float yaw = 0.6f, pitch = 0.28f, radius = 9.0f;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    bool ibl = true;
};
void OnMouse(GLFWwindow* w, double x, double y) {
    auto* v = static_cast<View*>(glfwGetWindowUserPointer(w));
    if (!v || !v->dragging) return;
    v->yaw -= static_cast<float>(x - v->lastX) * 0.006f;
    v->pitch = std::min(std::max(v->pitch + static_cast<float>(y - v->lastY) * 0.006f, -1.45f), 1.45f);
    v->lastX = x; v->lastY = y;
}
void OnButton(GLFWwindow* w, int b, int a, int) {
    if (b != GLFW_MOUSE_BUTTON_LEFT) return;
    if (auto* v = static_cast<View*>(glfwGetWindowUserPointer(w))) {
        v->dragging = (a == GLFW_PRESS); glfwGetCursorPos(w, &v->lastX, &v->lastY);
    }
}
struct Item { gldx::Mesh mesh; gldx::PbrMaterial material; };

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {"ibl"}, {"yaw", "pitch", "radius"}, "ibl_environment");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - ibl_environment (sky-driven image-based lighting)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    GLFWwindow* const native = window.Handle();
    gldx::Renderer renderer;
    renderer.Init();

    View view;
    view.yaw = flags.real("yaw", view.yaw);
    view.pitch = flags.real("pitch", view.pitch);
    view.radius = flags.real("radius", view.radius, 2.0f, 40.0f);
    view.ibl = flags.on("ibl");
    glfwSetWindowUserPointer(native, &view);
    glfwSetCursorPosCallback(native, OnMouse);
    glfwSetMouseButtonCallback(native, OnButton);

    renderer.AddPass(std::make_unique<gldx::GeometryPass>());
    renderer.AddPass(std::make_unique<gldx::SkyboxPass>());

    auto pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex, gldx::shaders::kPbrFragment);
    if (!pbr) { std::fprintf(stderr, "PBR shader: %s\n", pbr.error().c_str()); return 1; }
    pbr->Use();
    pbr->Set("uIrradiance", static_cast<int>(gldx::texunit::irradiance));
    pbr->Set("uPrefilter",  static_cast<int>(gldx::texunit::prefilter));
    pbr->Set("uBrdfLut",    static_cast<int>(gldx::texunit::brdfLut));
    pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);

    gldx::SkyboxRenderer skybox;
    if (!skybox.Init()) { std::fprintf(stderr, "Skybox init failed\n"); return 1; }

    const glm::vec3 towardSun = glm::normalize(glm::vec3(0.4f, 0.55f, 0.3f));
    gldx::EnvironmentMap env;
    if (!env.Generate(-towardSun, 256, 32, 256)) {   // Generate wants the travel direction
        std::fprintf(stderr, "Environment generation failed\n"); return 1;
    }

    gldx::LightBuffer lights; lights.Init();
    gldx::Camera camera; camera.SetPerspective(45.0f, 1.0f, 0.1f, 100.0f);

    std::vector<std::unique_ptr<Item>> items;
    gldx::Scene scene;
    auto add = [&](gldx::MeshData data, const gldx::PbrMaterial& m, const gldx::Transform& t) {
        auto it = std::make_unique<Item>(); it->material = m;
        glm::vec3 c; float r; gldx::SceneNode::BoundsFromMeshData(data, c, r);
        it->mesh.Upload(std::move(data));
        gldx::SceneNode& n = scene.CreateRoot(t);
        n.SetRenderable(&it->mesh, &it->material); n.SetLocalBounds(c, r);
        items.push_back(std::move(it));
        return &n;
    };
    { gldx::Transform t; t.scale = glm::vec3(18.0f, 1.0f, 18.0f);
      gldx::PbrMaterial m; m.baseColor = glm::vec4(0.5f, 0.5f, 0.55f, 1.0f); m.roughness = 0.9f;
      add(gldx::GeometryFactory::Plane(1.0f), m, t)->castsShadow = false; }
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {   // roughness rises across the row
        gldx::Transform t; t.translation = glm::vec3(-2.4f + i * 1.6f, 0.55f, -2.4f + j * 1.6f);
        gldx::PbrMaterial m; m.metallic = 1.0f;
        m.roughness = 0.05f + 0.9f * static_cast<float>(j) / 3.0f;
        m.baseColor = glm::vec4(0.9f, 0.85f, 0.8f, 1.0f);
        add(gldx::GeometryFactory::Sphere(0.55f, 40, 28), m, t);
    }

    static bool armed = true;
    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        if (bool p = glfwGetKey(info.window->Handle(), GLFW_KEY_2) == GLFW_PRESS; p && armed) { view.ibl = !view.ibl; armed = false; }
        else if (!p) armed = true;

        const glm::vec3 target(0.0f, 0.4f, 0.0f);
        const float cp = std::cos(view.pitch);
        const glm::vec3 eye(target.x + view.radius * cp * std::sin(view.yaw),
                            target.y + view.radius * std::sin(view.pitch),
                            target.z + view.radius * cp * std::cos(view.yaw));
        camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
        camera.LookAt(eye, target, glm::vec3(0, 1, 0));

        gldx::LightSetup setup;
        setup.sun.direction = -towardSun;
        setup.sun.color = glm::vec3(1.0f);
        setup.sun.intensity = 0.6f;   // sun kept weak so IBL dominates when on
        setup.ambient = view.ibl ? glm::vec3(0.0f) : glm::vec3(0.35f);
        lights.Update(setup, camera.Position());
        scene.Update();

        f.camera = &camera; f.scene = &scene; f.lights = &lights; f.pbr = &*pbr;
        f.viewProj = camera.ViewProjection(); f.lightSetup = setup;
        f.sky.box = &skybox; f.sky.env = &env;   // sky feeds both the background and IBL
        f.ibl.enabled = view.ibl;

        renderer.Render(f);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
