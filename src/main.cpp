/**
 * @file main.cpp
 * @brief Phase 1 graphics demo: GLFW 4.1 context, procedural textured cube,
 *        and an optional Assimp model loaded through the thread-safe AssetManager.
 *
 * Usage:  GLFW_Template [path/to/model.obj|fbx|gltf ...]
 * Without an argument it renders a spinning checkerboard cube. With a model
 * path it renders that model alongside the cube, exercising the async pipeline.
 */

#include "common.h"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "gfx/core/RenderContext.h"
#include "gfx/shader/ShaderProgram.h"
#include "gfx/geometry/GeometryFactory.h"
#include "gfx/geometry/Mesh.h"
#include "gfx/texture/Texture2D.h"
#include "gfx/camera/Camera.h"
#include "gfx/render/Renderer.h"
#include "gfx/assets/AssetManager.h"
#include "gfx/geometry/Model.h"

namespace {

const char* kVertexSrc = R"(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMatrix;
out vec3 vNormal;
out vec2 vUV;
out vec3 vWorldPos;
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUV = aUV;
    gl_Position = uViewProj * wp;
}
)";

const char* kFragmentSrc = R"(#version 410 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vWorldPos;
out vec4 FragColor;
uniform sampler2D uAlbedo;
uniform int  uHasAlbedo;
uniform vec3 uBaseColor;
uniform vec3 uLightDir;
uniform vec3 uCamPos;
void main() {
    vec3 base = uHasAlbedo == 1 ? texture(uAlbedo, vUV).rgb : uBaseColor;
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    float diff = max(dot(N, L), 0.0);
    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    vec3 color = base * (0.25 + 0.85 * diff) + vec3(1.0) * spec * 0.15;
    FragColor = vec4(color, 1.0);
}
)";

// CPU-side checkerboard: a Texture2DDesc (Stage A) with no GL, uploaded later.
gfx::Texture2DDesc MakeCheckerDesc(int size = 256, int cells = 8) {
    gfx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 3;
    d.srgb = true;
    d.pixels.resize(static_cast<std::size_t>(size) * size * 3);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool on = ((x / cell) + (y / cell)) % 2 == 0;
            const glm::vec3 c = on ? glm::vec3(0.85f, 0.75f, 0.35f) : glm::vec3(0.20f, 0.22f, 0.28f);
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            d.pixels[i + 0] = static_cast<std::uint8_t>(c.r * 255.0f);
            d.pixels[i + 1] = static_cast<std::uint8_t>(c.g * 255.0f);
            d.pixels[i + 2] = static_cast<std::uint8_t>(c.b * 255.0f);
        }
    }
    return d;
}

void DrawWithAlbedo(const gfx::Mesh& mesh, const gfx::ShaderProgram& shader,
                    const std::shared_ptr<gfx::Texture2D>& albedo, const glm::mat4& model) {
    shader.Set("uModel", model);
    shader.Set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
    if (albedo && albedo->valid()) {
        albedo->Bind(0);
        shader.Set("uAlbedo", 0);
        shader.Set("uHasAlbedo", 1);
    } else {
        shader.Set("uHasAlbedo", 0);
        shader.Set("uBaseColor", glm::vec3(0.6f, 0.62f, 0.68f));
    }
    mesh.Draw();
}

} // namespace

int main(int argc, char** argv) {
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
        return 1;
    }

    // From here on we are on the render thread; enable thread-affinity guards.
    gfx::RenderContext::MarkAsRenderThread();

    std::printf("%s %s\n", APP_NAME, APP_VERSION);
    std::printf("OpenGL %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    // All GPU-resource owners live in this scope so their destructors run while
    // the GL context is still current (before the window is destroyed).
    {
        auto shader = gfx::ShaderProgram::CreateFromSource(kVertexSrc, kFragmentSrc);
        if (!shader) {
            std::fprintf(stderr, "Shader error:\n%s\n", shader.error().c_str());
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }

        gfx::Renderer renderer;
        renderer.Init();
        gfx::AssetManager assets(2);

        // Procedural cube + checker texture (uploaded directly on the render thread).
        gfx::Mesh cube;
        cube.Upload(gfx::GeometryFactory::Cube(1.0f));
        auto checker = std::make_shared<gfx::Texture2D>();
        checker->Upload(MakeCheckerDesc());

        // Optional async model load through the thread-safe pipeline.
        std::string modelPath = argc > 1 ? argv[1] : "";
        if (!modelPath.empty()) {
            assets.RequestModel("demo", modelPath);
            std::printf("Requesting model: %s\n", modelPath.c_str());
        }

        gfx::Camera camera;
        camera.SetPerspective(45.0f, 1.0f, 0.1f, 200.0f);

        auto last = glfwGetTime();
        float angle = 0.0f;
        while (!glfwWindowShouldClose(window)) {
            const double now = glfwGetTime();
            const float dt = static_cast<float>(now - last);
            last = now;
            angle += dt * 0.6f;

            if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);

            assets.ProcessUploads();

            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(window, &fbw, &fbh);
            camera.SetViewportAspect(fbh > 0 ? static_cast<float>(fbw) / fbh : 1.0f);

            const glm::vec3 eye(3.5f, 2.5f, 5.0f);
            camera.LookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));

            renderer.BeginFrame(fbw, fbh);
            shader->Use();
            shader->Set("uViewProj", camera.ViewProjection());
            shader->Set("uCamPos", eye);
            shader->Set("uLightDir", glm::normalize(glm::vec3(-0.5f, -1.0f, -0.35f)));

            const glm::mat4 cubeModel =
                glm::translate(glm::mat4(1.0f), glm::vec3(-1.4f, 0.0f, 0.0f)) *
                glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.3f, 1.0f, 0.1f));
            DrawWithAlbedo(cube, *shader, checker, cubeModel);

            if (auto model = assets.GetModel("demo")) {
                const glm::mat4 modelMat =
                    glm::translate(glm::mat4(1.0f), glm::vec3(1.6f, 0.0f, 0.0f)) *
                    glm::rotate(glm::mat4(1.0f), -angle, glm::vec3(0, 1, 0)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(model->scale));
                for (const auto& mesh : model->meshes) {
                    for (const auto& range : mesh->ranges()) {
                        std::shared_ptr<gfx::Texture2D> tex;
                        if (range.materialIndex >= 0 &&
                            static_cast<std::size_t>(range.materialIndex) < model->textures.size()) {
                            tex = model->textures[range.materialIndex];
                        }
                        shader->Set("uModel", modelMat);
                        shader->Set("uNormalMatrix",
                                    glm::transpose(glm::inverse(glm::mat3(modelMat))));
                        if (tex && tex->valid()) {
                            tex->Bind(0);
                            shader->Set("uAlbedo", 0);
                            shader->Set("uHasAlbedo", 1);
                        } else {
                            shader->Set("uHasAlbedo", 0);
                            shader->Set("uBaseColor", glm::vec3(0.7f, 0.7f, 0.75f));
                        }
                        mesh->DrawRange(range);
                    }
                }
            }

            renderer.EndFrame();
            glfwSwapBuffers(window);
            glfwPollEvents();
        }
        // assets/checker/cube/shader destruct here, on the render thread.
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
