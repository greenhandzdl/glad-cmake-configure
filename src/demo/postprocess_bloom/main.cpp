/**
 * @file main.cpp
 * @brief postprocess_bloom - the HDR / MSAA / bloom / ACES composite chain.
 *
 * This demo is exactly pbr_lighting's scene plus one thing: a PostProcessChain
 * handed to the frame as `f.post`. That single pointer changes how the other
 * passes run: GeometryPass now renders linear HDR into the chain's MSAA offscreen
 * target (BeginSceneTarget takes the `post != nullptr` branch), and PostProcessPass
 * resolves it, runs the bright-pass + separable blur when `useBloom` is set, then
 * tone-maps (ACES) and composites onto the window. Without a chain the same scene
 * renders straight at the window and no composite runs - so bloom / MSAA / HDR are
 * genuinely opt-in, not a required part of drawing a scene.
 *
 * `bloom` (default on) toggles the glow so the difference is obvious on the metal
 * spheres' sun glints. Controls: 3 toggles bloom, Esc quits, drag orbits,
 * --on/--off bloom, --quit-after SECONDS for headless.
 */

#include "demo/demo_app.h"

#include <cmath>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

struct View {
    float yaw = 0.65f, pitch = 0.35f, radius = 10.0f;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    bool bloom = true;
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
    const demo::Flags flags(argc, argv, {"bloom"}, {"yaw", "pitch", "radius"}, "postprocess_bloom");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    return demo::Run(flags, "gldx demo - postprocess_bloom (HDR + MSAA + bloom + ACES)",
                     [&](demo::Ctx& ctx) -> int {
        View view;
        view.yaw = flags.real("yaw", view.yaw);
        view.pitch = flags.real("pitch", view.pitch);
        view.radius = flags.real("radius", view.radius, 2.0f, 40.0f);
        view.bloom = flags.on("bloom");
        glfwSetWindowUserPointer(ctx.window, &view);
        glfwSetCursorPosCallback(ctx.window, OnMouse);
        glfwSetMouseButtonCallback(ctx.window, OnButton);

        ctx.renderer.AddPass(std::make_unique<gldx::GeometryPass>());
        ctx.renderer.AddPass(std::make_unique<gldx::PostProcessPass>());

        auto pbr = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kPbrVertex, gldx::shaders::kPbrFragment);
        if (!pbr) { std::fprintf(stderr, "PBR shader: %s\n", pbr.error().c_str()); return 1; }
        pbr->Use();
        pbr->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);

        gldx::PostProcessChain post;
        if (!post.Init()) { std::fprintf(stderr, "PostProcessChain init failed\n"); return 1; }
        post.SetExposure(1.15f);

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
        };
        { gldx::Transform t; t.scale = glm::vec3(16.0f, 1.0f, 16.0f);
          gldx::PbrMaterial m; m.baseColor = glm::vec4(0.4f, 0.42f, 0.46f, 1.0f); m.roughness = 0.95f;
          add(gldx::GeometryFactory::Plane(1.0f), m, t); }
        for (int i = 0; i < 5; ++i) {   // a row of polished metal balls: bright, camera-facing glints
            gldx::Transform t; t.translation = glm::vec3(-2.8f + i * 1.4f, 0.5f, 0.0f);
            gldx::PbrMaterial m; m.metallic = 1.0f; m.roughness = 0.12f + 0.15f * i;
            m.baseColor = glm::vec4(1.0f, 0.6f - 0.08f * i, 0.25f, 1.0f);
            add(gldx::GeometryFactory::Sphere(0.5f, 48, 32), m, t);
        }

        static bool armed = true;
        return ctx.Loop([&](gldx::RenderFrame& f, const demo::FrameInfo& info) {
            if (bool p = glfwGetKey(info.window, GLFW_KEY_3) == GLFW_PRESS; p && armed) { view.bloom = !view.bloom; armed = false; }
            else if (!p) armed = true;

            const glm::vec3 target(0.0f, 0.4f, 0.0f);
            const float cp = std::cos(view.pitch);
            const glm::vec3 eye(target.x + view.radius * cp * std::sin(view.yaw),
                                target.y + view.radius * std::sin(view.pitch),
                                target.z + view.radius * cp * std::cos(view.yaw));
            camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
            camera.LookAt(eye, target, glm::vec3(0, 1, 0));

            gldx::LightSetup setup;
            setup.sun.direction = glm::normalize(glm::vec3(-0.5f, -0.9f, 0.4f));
            setup.sun.color = glm::vec3(1.0f);
            setup.sun.intensity = 4.0f;   // hot sun -> real HDR glints for the bright pass
            setup.ambient = glm::vec3(0.05f);
            lights.Update(setup, camera.Position());
            scene.Update();

            f.camera = &camera; f.scene = &scene; f.lights = &lights; f.pbr = &*pbr;
            f.viewProj = camera.ViewProjection(); f.lightSetup = setup;
            f.post = &post;              // non-null -> 3D passes render HDR offscreen
            f.useBloom = view.bloom;
        });
    });
}
