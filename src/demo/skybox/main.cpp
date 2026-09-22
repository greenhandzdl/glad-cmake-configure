/**
 * @file main.cpp
 * @brief skybox - the HDR environment cube as a standalone background pass.
 *
 * SkyboxPass is its own stage: the opaque pass (here GeometryPass) opens the
 * scene target and leaves it bound, then SkyboxPass fills the pixels nothing
 * covered using the LEQUAL-depth trick, before PostProcessPass closes and
 * tone-maps the frame. It draws straight from EnvironmentMap's source sky cube
 * while IBL sampling is left OFF (f.ibl.enabled == false) - so this is "just the
 * background", the sky as scenery rather than as a light source. The scene is a
 * couple of sun-lit boxes; the cube map's horizon is the subject.
 *
 * Controls: 6 toggles the sky on/off (off reveals the flat clear colour), Esc
 * quits, drag orbits, --on/--off sky, --quit-after SECONDS for headless.
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
    float yaw = 0.6f, pitch = -0.05f, radius = 7.0f;   // near-horizon to show the sky band
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    bool sky = true;
};
struct Item { gldx::Mesh mesh; gldx::PbrMaterial material; };

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {"sky"}, {"yaw", "pitch", "radius"}, "skybox");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - skybox (environment cube as background)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();

    View view;
    view.yaw = flags.real("yaw", view.yaw);
    view.pitch = flags.real("pitch", view.pitch);
    view.radius = flags.real("radius", view.radius, 2.0f, 40.0f);
    view.sky = flags.on("sky");
    // Drag-to-orbit lives on the gldxwin input surface now: no GLFW
    // callbacks, no user-pointer, no GLFW constants in demo code.
    window.OnCursor([&view](gldx::win::Window&, gldx::win::Vec2d pos) {
        if (!view.dragging) return;
        view.yaw -= static_cast<float>(pos.x - view.lastX) * 0.006f;
        view.pitch = std::min(std::max(view.pitch + static_cast<float>(pos.y - view.lastY) * 0.006f, -1.45f), 1.45f);
        view.lastX = pos.x; view.lastY = pos.y;
    });
    window.OnMouseButton([&view](gldx::win::Window& w, gldx::win::MouseButton button,
                              gldx::win::KeyAction action, int) {
        if (button != gldx::win::MouseButton::Left) return;
        view.dragging = (action == gldx::win::KeyAction::Press);
        const gldx::win::Vec2d c = w.CursorPos();
        view.lastX = c.x; view.lastY = c.y;
    });

    // Geometry opens the target -> Skybox fills uncovered pixels -> Post closes.
    renderer.AddPass(std::make_unique<gldx::GeometryPass>());
    renderer.AddPass(std::make_unique<gldx::SkyboxPass>());
    renderer.AddPass(std::make_unique<gldx::PostProcessPass>());

    auto pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex, gldx::shaders::kPbrFragment);
    if (!pbr) { std::fprintf(stderr, "PBR shader: %s\n", pbr.error().c_str()); return 1; }
    pbr->Use();
    pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);

    gldx::SkyboxRenderer skybox;
    if (!skybox.Init()) { std::fprintf(stderr, "Skybox init failed\n"); return 1; }
    const glm::vec3 towardSun = glm::normalize(glm::vec3(0.5f, 0.35f, 0.4f));
    gldx::EnvironmentMap env;
    if (!env.Generate(-towardSun, 256, 32, 256)) { std::fprintf(stderr, "Env gen failed\n"); return 1; }

    gldx::PostProcessChain post;
    if (!post.Init()) { std::fprintf(stderr, "Post init failed\n"); return 1; }
    post.SetExposure(1.0f);

    gldx::LightBuffer lights; lights.Init();
    gldx::Camera camera; camera.SetPerspective(55.0f, 1.0f, 0.1f, 500.0f);

    std::vector<std::unique_ptr<Item>> items;
    gldx::Scene scene;
    auto add = [&](gldx::MeshData data, const gldx::PbrMaterial& m, const gldx::Transform& t) {
        auto it = std::make_unique<Item>(); it->material = m;
        glm::vec3 c; float r; gldx::SceneNode::BoundsFromMeshData(data, c, r);
        it->mesh.Upload(std::move(data));
        gldx::SceneNode& n = scene.CreateRoot(t);
        n.SetRenderable(&it->mesh, &it->material); n.SetLocalBounds(c, r);
        items.push_back(std::move(it));
    };
    { gldx::Transform t; t.scale = glm::vec3(30.0f, 1.0f, 30.0f);
      gldx::PbrMaterial m; m.baseColor = glm::vec4(0.35f, 0.37f, 0.4f, 1.0f); m.roughness = 0.95f;
      add(gldx::GeometryFactory::Plane(1.0f), m, t); }
    for (int k = 0; k < 3; ++k) {
        gldx::Transform t; t.translation = glm::vec3(-1.8f + k * 1.8f, 0.6f, 0.0f);
        t.SetAxisAngle(glm::vec3(0, 1, 0), 0.5f * k);
        gldx::PbrMaterial m; m.baseColor = glm::vec4(0.8f, 0.8f, 0.85f, 1.0f); m.roughness = 0.5f;
        add(gldx::GeometryFactory::Cube(1.0f), m, t);
    }

    static bool armed = true;
    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        if (bool p = info.window->KeyIsDown(gldx::win::Key::Num6); p && armed) { view.sky = !view.sky; armed = false; }
        else if (!p) armed = true;

        const glm::vec3 target(0.0f, 0.6f, 0.0f);
        const float cp = std::cos(view.pitch);
        const glm::vec3 eye(target.x + view.radius * cp * std::sin(view.yaw),
                            target.y + view.radius * std::sin(view.pitch),
                            target.z + view.radius * cp * std::cos(view.yaw));
        camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
        camera.LookAt(eye, target, glm::vec3(0, 1, 0));

        gldx::LightSetup setup;
        setup.sun.direction = -towardSun;
        setup.sun.color = glm::vec3(1.0f);
        setup.sun.intensity = 2.5f;
        setup.ambient = glm::vec3(0.15f);   // flat ambient: the sky lights nothing here
        lights.Update(setup, camera.Position());
        scene.Update();

        f.camera = &camera; f.scene = &scene; f.lights = &lights; f.pbr = &*pbr;
        f.viewProj = camera.ViewProjection(); f.lightSetup = setup;
        f.post = &post;
        f.sky.box = view.sky ? &skybox : nullptr;   // null => no background drawn
        f.sky.env = &env;
        f.ibl.enabled = false;                       // sky as scenery, not as a light source

        renderer.Render(f);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
