/**
 * @file main.cpp
 * @brief pbr_lighting - the scene minimum: a camera, one lighting UBO, the PBR
 *        program and a scene graph. Nothing else.
 *
 * This is the first "real" demo above the hello-triangle and deliberately brings
 * only what GeometryPass hard-requires (camera + lights + pbr + scene, see
 * RenderPasses.cpp). No post chain, no shadow, no IBL, no skybox: the geometry
 * pass renders linear HDR straight at the default framebuffer (BeginSceneTarget
 * with post == nullptr). A metallic/roughness test grid plus a spinning cube
 * show that base-color / metallic / roughness alone already carry the whole PBR
 * look - everything else in the engine is opt-in on top of this.
 *
 * Controls: Esc quits, drag orbits, --quit-after SECONDS for headless runs.
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

// Orbit camera state, nudged by the mouse.
struct View {
    float yaw = 0.7f, pitch = 0.35f, radius = 9.0f;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
};

// Owns a GPU mesh + its CPU material for one node (addresses stay stable).
struct Item {
    gldx::Mesh mesh;
    gldx::PbrMaterial material;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"yaw", "pitch", "radius"}, "pbr_lighting");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - pbr_lighting (camera + lights + PBR + scene)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();

    View view;
    view.yaw = flags.real("yaw", view.yaw);
    view.pitch = flags.real("pitch", view.pitch);
    view.radius = flags.real("radius", view.radius, 2.0f, 40.0f);
    // Drag-to-orbit lives on the gldxwin input surface now: no GLFW
    // callbacks, no user-pointer, no GLFW constants in demo code.
    window.OnCursor([&view](gldx::win::Window&, gldx::win::Vec2d pos) {
        if (!view.dragging) return;
        view.yaw   -= static_cast<float>(pos.x - view.lastX) * 0.006f;
        view.pitch += static_cast<float>(pos.y - view.lastY) * 0.006f;
        if (view.pitch > 1.45f) view.pitch = 1.45f;
        if (view.pitch < -1.45f) view.pitch = -1.45f;
        view.lastX = pos.x; view.lastY = pos.y;
    });
    window.OnMouseButton([&view](gldx::win::Window& w, gldx::win::MouseButton button,
                                  gldx::win::KeyAction action, int) {
        if (button != gldx::win::MouseButton::Left) return;
        view.dragging = (action == gldx::win::KeyAction::Press);
        const gldx::win::Vec2d c = w.CursorPos();
        view.lastX = c.x; view.lastY = c.y;
    });

    renderer.AddPass(std::make_unique<gldx::GeometryPass>());   // only the pass the scene needs

    auto pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex, gldx::shaders::kPbrFragment);
    if (!pbr) { std::fprintf(stderr, "PBR shader: %s\n", pbr.error().c_str()); return 1; }
    pbr->Use();
    pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);

    gldx::LightBuffer lights;
    lights.Init();

    gldx::Camera camera;
    camera.SetPerspective(45.0f, 1.0f, 0.1f, 100.0f);

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
    };

    {   // ground (never casts — no shadow subsystem here anyway).
        gldx::Transform t; t.scale = glm::vec3(20.0f, 1.0f, 20.0f);
        gldx::PbrMaterial m; m.baseColor = glm::vec4(0.55f, 0.56f, 0.6f, 1.0f); m.roughness = 0.9f;
        add(gldx::GeometryFactory::Plane(1.0f), m, t);
    }
    for (int i = 0; i < 5; ++i) for (int j = 0; j < 5; ++j) {   // metal x roughness grid
        gldx::Transform t; t.translation = glm::vec3(-3.0f + i * 1.5f, 0.5f, -3.0f + j * 1.5f);
        gldx::PbrMaterial m;
        m.metallic = static_cast<float>(i) / 4.0f;
        m.roughness = 0.05f + 0.9f * static_cast<float>(j) / 4.0f;
        m.baseColor = glm::vec4(0.9f, 0.5f, 0.25f, 1.0f);
        add(gldx::GeometryFactory::Sphere(0.5f, 32, 24), m, t);
    }

    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        const glm::vec3 target(0.0f, 0.5f, 0.0f);
        const float cp = std::cos(view.pitch);
        const glm::vec3 eye(target.x + view.radius * cp * std::sin(view.yaw),
                            target.y + view.radius * std::sin(view.pitch),
                            target.z + view.radius * cp * std::cos(view.yaw));
        camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
        camera.LookAt(eye, target, glm::vec3(0, 1, 0));

        gldx::LightSetup setup;
        setup.sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
        setup.sun.color = glm::vec3(1.0f);
        setup.sun.intensity = 3.0f;
        setup.ambient = glm::vec3(0.08f);
        lights.Update(setup, camera.Position());

        scene.Update();

        f.camera = &camera;
        f.scene = &scene;
        f.lights = &lights;
        f.pbr = &*pbr;
        f.viewProj = camera.ViewProjection();
        f.lightSetup = setup;

        renderer.Render(f);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
