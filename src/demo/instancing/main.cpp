/**
 * @file main.cpp
 * @brief instancing - one base mesh drawn many times in a single call.
 *
 * InstancedMesh pairs a geometry VBO (position + normal) with an instance
 * buffer whose per-instance mat4 rides on vertex attributes 4-7 and a colour on
 * 8, all with divisor 1; kInstancedVertex/kInstancedFragment read them and light
 * the result through the same LightingBlock UBO the PBR program uses. GeometryPass
 * draws the field for you once the frame carries `f.instances.field` + a compiled
 * `f.instances.prog` + `enabled`, so the whole 8x8 tower grid is one
 * glDrawElementsInstanced regardless of how many boxes there are.
 *
 * The CPU data (MeshData + the std::vector<Instance>) is built freely (Stage A);
 * Create() is the render-thread upload (Stage B). `instances` toggles the field
 * so a headless run can diff the frame with it on vs off.
 *
 * Controls: 5 toggles the instanced field, Esc quits, drag orbits,
 * --on/--off instances, --quit-after SECONDS for headless.
 */

#include "demo/demo_app.h"

#include <cmath>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

struct View {
    float yaw = -0.9f, pitch = 0.42f, radius = 18.0f;   // aim toward the +x field
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    bool instances = true;
};
void OnMouse(GLFWwindow* w, double x, double y) {
    auto* v = static_cast<View*>(glfwGetWindowUserPointer(w));
    if (!v || !v->dragging) return;
    v->yaw   -= static_cast<float>(x - v->lastX) * 0.006f;
    v->pitch += static_cast<float>(y - v->lastY) * 0.006f;
    v->pitch = std::min(std::max(v->pitch, -1.45f), 1.45f);
    v->lastX = x; v->lastY = y;
}
void OnButton(GLFWwindow* w, int b, int a, int) {
    if (b != GLFW_MOUSE_BUTTON_LEFT) return;
    if (auto* v = static_cast<View*>(glfwGetWindowUserPointer(w))) {
        v->dragging = (a == GLFW_PRESS); glfwGetCursorPos(w, &v->lastX, &v->lastY);
    }
}

// The instanced field lives out toward +x; a couple of hero PBR spheres near the
// origin give the eye a scale reference and prove the two paths coexist.
struct Item { gfx::Mesh mesh; gfx::PbrMaterial material; };

} // namespace

int main(int argc, char** argv) {
    const demo::Flags flags(argc, argv, {"instances"}, {"yaw", "pitch", "radius"}, "instancing");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    return demo::Run(flags, "gfx demo - instancing (one mesh, one draw call)",
                     [&](demo::Ctx& ctx) -> int {
        View view;
        view.yaw = flags.real("yaw", view.yaw);
        view.pitch = flags.real("pitch", view.pitch);
        view.radius = flags.real("radius", view.radius, 2.0f, 60.0f);
        view.instances = flags.on("instances");
        glfwSetWindowUserPointer(ctx.window, &view);
        glfwSetCursorPosCallback(ctx.window, OnMouse);
        glfwSetMouseButtonCallback(ctx.window, OnButton);

        ctx.renderer.AddPass(std::make_unique<gfx::GeometryPass>());

        auto pbr = gfx::ShaderProgram::CreateFromSource(gfx::shaders::kPbrVertex, gfx::shaders::kPbrFragment);
        if (!pbr) { std::fprintf(stderr, "PBR shader: %s\n", pbr.error().c_str()); return 1; }
        pbr->Use();
        pbr->SetBlockBinding("LightingBlock", gfx::LightBuffer::kBinding);

        // A second program just for the instanced field, same UBO binding point.
        auto instProg = gfx::ShaderProgram::CreateFromSource(
            gfx::shaders::kInstancedVertex, gfx::shaders::kInstancedFragment);
        if (!instProg) {
            std::fprintf(stderr, "Instanced shader: %s\n", instProg.error().c_str());
            return 1;
        }
        instProg->Use();
        instProg->SetBlockBinding("LightingBlock", gfx::LightBuffer::kBinding);

        gfx::LightBuffer lights; lights.Init();
        gfx::Camera camera; camera.SetPerspective(45.0f, 1.0f, 0.1f, 200.0f);

        std::vector<std::unique_ptr<Item>> items;
        gfx::Scene scene;
        auto add = [&](gfx::MeshData data, const gfx::PbrMaterial& m, const gfx::Transform& t) {
            auto it = std::make_unique<Item>(); it->material = m;
            glm::vec3 c; float r; gfx::SceneNode::BoundsFromMeshData(data, c, r);
            it->mesh.Upload(std::move(data));
            gfx::SceneNode& n = scene.CreateRoot(t);
            n.SetRenderable(&it->mesh, &it->material); n.SetLocalBounds(c, r);
            items.push_back(std::move(it));
        };
        { gfx::Transform t; t.scale = glm::vec3(40.0f, 1.0f, 40.0f);
          gfx::PbrMaterial m; m.baseColor = glm::vec4(0.4f, 0.42f, 0.46f, 1.0f); m.roughness = 0.95f;
          add(gfx::GeometryFactory::Plane(1.0f), m, t); }
        for (int k = 0; k < 2; ++k) {   // scale reference spheres
            gfx::Transform t; t.translation = glm::vec3(-1.2f + k * 2.4f, 0.6f, -1.0f);
            gfx::PbrMaterial m; m.metallic = 0.9f; m.roughness = 0.25f;
            m.baseColor = glm::vec4(0.9f, 0.6f, 0.3f, 1.0f);
            add(gfx::GeometryFactory::Sphere(0.6f, 40, 28), m, t);
        }

        // Stage A: build the instance transforms on the CPU, then upload once.
        gfx::InstancedMesh field;
        {
            gfx::MeshData geo = gfx::GeometryFactory::Cube(1.0f);
            std::vector<gfx::Instance> insts;
            constexpr int n = 8;
            for (int ix = 0; ix < n; ++ix) {
                for (int iz = 0; iz < n; ++iz) {
                    const float x = 4.0f + ix * 1.5f;
                    const float z = -5.0f + iz * 1.5f;
                    const float h = 0.5f + 0.5f * static_cast<float>((ix * 3 + iz * 5) % 6);
                    const glm::mat4 m =
                        glm::translate(glm::mat4(1.0f), glm::vec3(x, h * 0.5f, z)) *
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, h, 0.5f));
                    gfx::Instance in;
                    in.model = m;
                    const float t = static_cast<float>((ix + iz) % 5) / 4.0f;
                    in.color = glm::vec4(0.3f + 0.6f * t, 0.45f, 0.85f - 0.5f * t, 1.0f);
                    insts.push_back(in);
                }
            }
            field.Create(std::move(geo), std::move(insts));
        }

        static bool armed = true;
        return ctx.Loop([&](gfx::RenderFrame& f, const demo::FrameInfo& info) {
            if (bool p = glfwGetKey(info.window, GLFW_KEY_5) == GLFW_PRESS; p && armed) { view.instances = !view.instances; armed = false; }
            else if (!p) armed = true;

            const glm::vec3 target(3.0f, 0.6f, 0.0f);   // look at the field
            const float cp = std::cos(view.pitch);
            const glm::vec3 eye(target.x + view.radius * cp * std::sin(view.yaw),
                                target.y + view.radius * std::sin(view.pitch),
                                target.z + view.radius * cp * std::cos(view.yaw));
            camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
            camera.LookAt(eye, target, glm::vec3(0, 1, 0));

            gfx::LightSetup setup;
            setup.sun.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
            setup.sun.color = glm::vec3(1.0f);
            setup.sun.intensity = 3.0f;
            setup.ambient = glm::vec3(0.08f);
            lights.Update(setup, camera.Position());
            scene.Update();

            f.camera = &camera; f.scene = &scene; f.lights = &lights; f.pbr = &*pbr;
            f.viewProj = camera.ViewProjection(); f.lightSetup = setup;
            f.instances.field = &field;
            f.instances.prog = &*instProg;
            f.instances.enabled = view.instances && field.valid();
        });
    });
}
