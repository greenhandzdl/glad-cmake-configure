/**
 * @file main.cpp
 * @brief geometry_upload - GeometryFactory -> MeshData -> Mesh::Upload -> Draw.
 *
 * This is the bare geometry pipeline with no lighting model, no material system
 * and no scene graph: the four CPU-only primitives GeometryFactory hands back
 * (Cube / Sphere / Plane / Quad, each a MeshData of gldx::Vertex + indices, built
 * freely on any thread = Stage A) are shipped to the GPU by Mesh::Upload on the
 * render thread (Stage B, which builds the VAO over an interleaved VBO + an EBO),
 * then drawn with a tiny inline unlit-but-shaded program.
 *
 * The vertex layout the VAO encodes is fixed by gldx::Vertex (ShaderLib and the
 * PBR/instanced shaders all agree on it): location 0 position, 1 normal, 2 uv,
 * 3 tangent, 4 colour. This demo's own GLSL only consumes 0 + 1 (a fixed half-
 * lambert term so the facets read), but every attribute is present in the buffer
 * - the point is the upload chain, not the shader.
 *
 * It deliberately does NOT go through GeometryPass/LightBuffer/PbrMaterial: a
 * Mesh is just a drawable buffer, and this shows drawing one with a program you
 * wrote yourself is all it takes. Controls: Esc quits, drag orbits,
 * --quit-after SECONDS for headless.
 */

#include "demo/demo_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

// Shaded-but-simple: position + normal in, a fixed light direction gives the
// facet a lambert term. `#version 410 core` matches the engine baseline.
constexpr const char* kVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vNormal;
void main() {
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
}
)GLSL";

constexpr const char* kFragment = R"GLSL(#version 410 core
in vec3 vNormal;
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNormal);
    float ndl = 0.4f + 0.6f * max(dot(n, normalize(vec3(0.4f, 0.8f, 0.3f))), 0.0f);
    FragColor = vec4(uColor * ndl, 1.0);
}
)GLSL";

struct Shape {
    gldx::Mesh mesh;
    glm::mat4 model{1.0f};
    glm::vec3 color{1.0f};
};

class UploadPass : public gldx::RenderPass {
public:
    UploadPass() : RenderPass("Upload") {
        auto p = gldx::ShaderProgram::CreateFromSource(kVertex, kFragment);
        if (!p) {
            std::fprintf(stderr, "unlit shader: %s\n", p.error().c_str());
            return;
        }
        program_ = std::make_unique<gldx::ShaderProgram>(std::move(*p));

        // Stage A (CPU MeshData) -> Stage B (upload) for each primitive.
        add(gldx::GeometryFactory::Cube(1.4f),
            glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.8f, 0.0f)),
            {0.85f, 0.35f, 0.30f});
        add(gldx::GeometryFactory::Sphere(1.0f, 48, 32),
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
            {0.30f, 0.65f, 0.90f});
        add(gldx::GeometryFactory::Quad(1.6f, 1.6f),
            glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 1.0f, 0.0f)),
                        0.4f, glm::vec3(0, 1, 0)),
            {0.95f, 0.75f, 0.25f});
        add(gldx::GeometryFactory::Plane(4.0f),
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)),
            {0.45f, 0.5f, 0.55f});
    }

    void Execute(gldx::RenderFrame& f) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(0.11f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!program_ || !f.camera) return;

        program_->Use();
        program_->Set("uViewProj", f.viewProj);
        for (Shape& s : shapes_) {
            program_->Set("uModel", s.model);
            program_->Set("uColor", s.color);
            s.mesh.Draw();
        }
    }

private:
    void add(gldx::MeshData data, const glm::mat4& model, const glm::vec3& color) {
        Shape s;
        s.model = model;
        s.color = color;
        s.mesh.Upload(std::move(data));   // Stage B, render thread
        shapes_.push_back(std::move(s));
    }

    std::unique_ptr<gldx::ShaderProgram> program_;
    std::vector<Shape> shapes_;
};

struct View {
    float yaw = 0.7f, pitch = 0.3f, radius = 8.0f;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
};
void OnMouse(GLFWwindow* w, double x, double y) {
    auto* v = static_cast<View*>(glfwGetWindowUserPointer(w));
    if (!v || !v->dragging) return;
    v->yaw   -= static_cast<float>(x - v->lastX) * 0.006f;
    v->pitch  = std::min(std::max(v->pitch + static_cast<float>(y - v->lastY) * 0.006f, -1.45f), 1.45f);
    v->lastX = x; v->lastY = y;
}
void OnButton(GLFWwindow* w, int b, int a, int) {
    if (b != GLFW_MOUSE_BUTTON_LEFT) return;
    if (auto* v = static_cast<View*>(glfwGetWindowUserPointer(w))) {
        v->dragging = (a == GLFW_PRESS); glfwGetCursorPos(w, &v->lastX, &v->lastY);
    }
}

} // namespace

int main(int argc, char** argv) {
    const demo::Flags flags(argc, argv, {}, {"yaw", "pitch", "radius"}, "geometry_upload");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    return demo::Run(flags, "gldx demo - geometry_upload (MeshData -> Mesh -> draw)",
                     [&](demo::Ctx& ctx) -> int {
        View view;
        view.yaw = flags.real("yaw", view.yaw);
        view.pitch = flags.real("pitch", view.pitch);
        view.radius = flags.real("radius", view.radius, 2.0f, 40.0f);
        glfwSetWindowUserPointer(ctx.window, &view);
        glfwSetCursorPosCallback(ctx.window, OnMouse);
        glfwSetMouseButtonCallback(ctx.window, OnButton);

        ctx.renderer.AddPass(std::make_unique<UploadPass>());

        gldx::Camera camera; camera.SetPerspective(50.0f, 1.0f, 0.1f, 100.0f);

        return ctx.Loop([&](gldx::RenderFrame& f, const demo::FrameInfo& info) {
            const glm::vec3 target(0.0f, 0.6f, 0.0f);
            const float cp = std::cos(view.pitch);
            const glm::vec3 eye(target.x + view.radius * cp * std::sin(view.yaw),
                                target.y + view.radius * std::sin(view.pitch),
                                target.z + view.radius * cp * std::cos(view.yaw));
            camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
            camera.LookAt(eye, target, glm::vec3(0, 1, 0));

            f.camera = &camera;
            f.viewProj = camera.ViewProjection();
        });
    });
}
