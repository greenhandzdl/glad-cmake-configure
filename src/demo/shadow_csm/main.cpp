/**
 * @file main.cpp
 * @brief shadow_csm - cascaded shadow maps, the one extra pass they need.
 *
 * Same lit scene as pbr_lighting plus the two shadow pieces: a CascadedShadowMap
 * (a depth texture array + a ShadowBlock UBO), the depth-only program that fills
 * it, and ShadowPass ahead of GeometryPass. The scene's sun is angled low so the
 * spheres throw long, readable shadows across the ground. GeometryPass already
 * binds the cascade array and toggles uUseShadow off `shadow.enabled &&
 * shadow.map`, so a frame that never brings the shadow record renders exactly
 * like the no-shadow case - which is the whole point of the split.
 *
 * Controls: 1 toggles shadows, Esc quits, drag orbits, --on/--off shadow,
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
    float yaw = 0.7f, pitch = 0.35f, radius = 10.0f;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    bool shadow = true;
};


struct Item { gldx::Mesh mesh; gldx::PbrMaterial material; };

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {"shadow"}, {"yaw", "pitch", "radius"}, "shadow_csm");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - shadow_csm (cascaded directional shadows)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();

    View view;
    view.yaw = flags.real("yaw", view.yaw);
    view.pitch = flags.real("pitch", view.pitch);
    view.radius = flags.real("radius", view.radius, 2.0f, 40.0f);
    view.shadow = flags.on("shadow");
    // Drag-to-orbit lives on the gldxwin input surface now: no GLFW
    // callbacks, no user-pointer, no GLFW constants in demo code.
    window.OnCursor([&view](gldx::win::Window&, gldx::win::Vec2d pos) {
        if (!view.dragging) return;
        view.yaw   -= static_cast<float>(pos.x - view.lastX) * 0.006f;
        view.pitch  = std::min(std::max(view.pitch + static_cast<float>(pos.y - view.lastY) * 0.006f, -1.45f), 1.45f);
        view.lastX = pos.x; view.lastY = pos.y;
    });
    window.OnMouseButton([&view](gldx::win::Window& w, gldx::win::MouseButton button,
                              gldx::win::KeyAction action, int) {
        if (button != gldx::win::MouseButton::Left) return;
        view.dragging = (action == gldx::win::KeyAction::Press);
        const gldx::win::Vec2d c = w.CursorPos();
        view.lastX = c.x; view.lastY = c.y;
    });

    renderer.AddPass(std::make_unique<gldx::ShadowPass>());
    renderer.AddPass(std::make_unique<gldx::GeometryPass>());

    auto pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex, gldx::shaders::kPbrFragment);
    if (!pbr) { std::fprintf(stderr, "PBR shader: %s\n", pbr.error().c_str()); return 1; }
    pbr->Use();
    pbr->Set("uShadowMap", static_cast<int>(gldx::texunit::shadowArray));
    pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);
    pbr->SetBlockBinding("ShadowBlock", gldx::CascadedShadowMap::kShadowBinding);

    auto depth = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kDepthVertex, gldx::shaders::kDepthFragment);
    if (!depth) { std::fprintf(stderr, "Depth shader: %s\n", depth.error().c_str()); return 1; }

    gldx::LightBuffer lights; lights.Init();
    gldx::CascadedShadowMap csm; csm.Init(2048);
    gldx::Camera camera; camera.SetPerspective(45.0f, 1.0f, 0.1f, 100.0f);

    std::vector<std::unique_ptr<Item>> items;
    gldx::Scene scene;
    auto add = [&](gldx::MeshData data, const gldx::PbrMaterial& m, const gldx::Transform& t) {
        auto it = std::make_unique<Item>();
        it->material = m;
        glm::vec3 c; float r;
        gldx::SceneNode::BoundsFromMeshData(data, c, r);
        it->mesh.Upload(std::move(data));
        gldx::SceneNode& n = scene.CreateRoot(t);
        n.SetRenderable(&it->mesh, &it->material);
        n.SetLocalBounds(c, r);
        items.push_back(std::move(it));
        return &n;
    };
    { gldx::Transform t; t.scale = glm::vec3(24.0f, 1.0f, 24.0f);
      gldx::PbrMaterial m; m.baseColor = glm::vec4(0.6f, 0.62f, 0.66f, 1.0f); m.roughness = 0.9f;
      add(gldx::GeometryFactory::Plane(1.0f), m, t)->castsShadow = false; }   // receives, never casts
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
        gldx::Transform t; t.translation = glm::vec3(-2.4f + i * 2.4f, 0.7f, -2.4f + j * 2.4f);
        gldx::PbrMaterial m; m.baseColor = glm::vec4(0.85f, 0.45f, 0.25f, 1.0f); m.roughness = 0.4f;
        add(gldx::GeometryFactory::Sphere(0.7f, 32, 24), m, t);
    }

    static bool armed = true;
    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        if (bool p = info.window->KeyIsDown(gldx::win::Key::Num1); p && armed) { view.shadow = !view.shadow; armed = false; }
        else if (!p) armed = true;

        const glm::vec3 target(0.0f, 0.4f, 0.0f);
        const float cp = std::cos(view.pitch);
        const glm::vec3 eye(target.x + view.radius * cp * std::sin(view.yaw),
                            target.y + view.radius * std::sin(view.pitch),
                            target.z + view.radius * cp * std::cos(view.yaw));
        camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
        camera.LookAt(eye, target, glm::vec3(0, 1, 0));

        const glm::vec3 towardSun = glm::normalize(glm::vec3(0.45f, 0.5f, 0.35f));   // low-ish sun => long shadows

        gldx::LightSetup setup;
        setup.sun.direction = -towardSun;
        setup.sun.color = glm::vec3(1.0f);
        setup.sun.intensity = 3.0f;
        setup.ambient = glm::vec3(0.12f);
        lights.Update(setup, camera.Position());
        scene.Update();

        f.camera = &camera; f.scene = &scene; f.lights = &lights; f.pbr = &*pbr;
        f.viewProj = camera.ViewProjection(); f.lightSetup = setup;
        f.shadow.map = &csm; f.shadow.depth = &*depth;
        f.shadow.sunToward = towardSun; f.shadow.enabled = view.shadow;

        renderer.Render(f);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
