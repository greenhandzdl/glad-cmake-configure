/**
 * @file main.cpp
 * @brief The smallest thing that can put pixels on screen: a bare hello-triangle.
 *
 * This is the engine's "minimum implementation" demo, kept in src/ root on
 * purpose so the primary `GLFW_Template` target (and the CI artifact name that
 * hangs off it) stays stable. Its whole point is subtraction: unlike the
 * feature demos, it brings *none* of the render subsystems - no GeometryPass,
 * no LightBuffer, no PBR program, no scene graph, no shadow / IBL / bloom /
 * skybox. It defines one custom gldx::RenderPass holding an inline GLSL program
 * and a gldx::VertexArray + gldx::GLBuffer, and hands the renderer an
 * otherwise-empty RenderFrame.
 * If this builds and draws, then "import gldx + one pass + one window" really
 * is the floor of the whole engine - and it is reached purely by importing the
 * three libraries (gldx / gldxwin / gldxcli), no demo scaffolding header.
 *
 * The window / context / frame-loop lifecycle is gldxwin's App + Window; the
 * only per-frame callback just fills fbWidth/fbHeight and renders, because the
 * triangle pass needs no data from the frame.
 *
 * Controls: Esc quits. --quit-after SECONDS ends a scripted/headless run.
 */

// Plain text include first: it orders <glad/gl.h> before <GLFW/glfw3.h> and
// carries the GLFW/GLAD declarations this TU references directly (GL_* enums,
// GLuint, glfwSwapBuffers via gldxwin, ...). The three libraries are then imported.
#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <cstdio>
#include <memory>
#include <span>
#include <utility>

namespace {

// A self-contained colored triangle: positions in the XY plane (location 0) and
// an rgb colour (location 1). `#version 410 core` matches the project's OpenGL
// 4.1 baseline; no engine shader header is involved.
constexpr const char* kVertex = R"GLSL(#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec3 aColor;
out vec3 vColor;
void main() {
    vColor = aColor;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFragment = R"GLSL(#version 410 core
in vec3 vColor;
out vec4 FragColor;
void main() { FragColor = vec4(vColor, 1.0); }
)GLSL";

// 3 vertices, interleaved [x, y, r, g, b]; the classic first triangle.
const GLfloat kVertices[] = {
     0.0f,  0.6f,   1.0f, 0.35f, 0.30f,   // top, red
    -0.6f, -0.5f,   0.30f, 1.0f, 0.40f,   // bottom-left, green
     0.6f, -0.5f,   0.30f, 0.55f, 1.0f,   // bottom-right, blue
};

// One pass that owns a program + a VAO and renders straight at the default
// framebuffer. Built on the render thread (Run has made the context current
// before the app callback constructs it), torn down inside it, so both the GL
// calls here and the ShaderProgram destructor see a live context.
class TrianglePass : public gldx::RenderPass {
public:
    TrianglePass() : RenderPass("Triangle") {
        auto program = gldx::ShaderProgram::CreateFromSource(kVertex, kFragment);
        if (!program) {
            std::fprintf(stderr, "Triangle shader error:\n%s\n", program.error().c_str());
            return;   // Execute() then becomes a no-op clear; the run still ends cleanly.
        }
        program_ = std::make_unique<gldx::ShaderProgram>(std::move(*program));

        // The engine's own RAII wrappers instead of raw glGen/glBind/glBufferData:
        // GLBuffer::Create uploads from a typed span and self-unbinds, so the
        // bindings the VAO must capture are recorded explicitly inside its scope.
        vbo_.Create(GL_ARRAY_BUFFER, std::span<const GLfloat>(kVertices));
        constexpr GLsizei stride = 5 * sizeof(GLfloat);
        vao_.Create();
        vao_.Bind();
        vbo_.Bind(GL_ARRAY_BUFFER);
        vao_.AttachAttribute(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
        vao_.AttachAttribute(1, 3, GL_FLOAT, GL_FALSE, stride,
                             reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        vao_.Unbind();
    }

    // vao_ / vbo_ are gldx RAII wrappers: their destructors delete the GL names
    // on the render thread (the pass is torn down inside Run, context still current).

    void Execute(gldx::RenderFrame& frame) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, frame.fbWidth, frame.fbHeight);
        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (program_) {
            program_->Use();
            vao_.Bind();
            vao_.DrawArrays(GL_TRIANGLES, 0, 3);
            vao_.Unbind();
        }
    }

private:
    std::unique_ptr<gldx::ShaderProgram> program_;
    gldx::VertexArray vao_;
    gldx::GLBuffer    vbo_;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {}, "GLFW_Template");
    if (flags.wantsHelp()) {
        flags.printUsage();
        return 0;
    }

    // gldxwin brings the window up (glfwInit via the App singleton, GL 4.1 core
    // hints, makeContextCurrent, gladLoadGL). Nothing on the engine side exists
    // until this returns true.
    gldx::win::WindowDesc desc;
    desc.title = "gldx - hello triangle (minimum pipeline)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    // The engine's single-render-thread guard: claimed here, at the top of the
    // render-thread work, because gldxwin is deliberately engine-agnostic.
    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();
    renderer.AddPass(std::make_unique<TrianglePass>());

    // The triangle pass reads only fb size off the frame, so the per-frame
    // callback just forwards it and renders - the whole "attach nothing" point.
    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame frame;
        frame.fbWidth     = info.fbWidth;
        frame.fbHeight    = info.fbHeight;
        frame.smoothedFps = info.smoothedFps;
        renderer.Render(frame);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
