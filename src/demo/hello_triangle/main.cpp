/**
 * @file main.cpp
 * @brief The same hello-triangle as `src/main.cpp`, assembled from the bottom
 *        up: gldx::VertexArray + gldx::GLBuffer + explicit AttachAttribute.
 *
 * This is the low-layer counterpart of the GLFW_Template target. src/main.cpp
 * reaches the screen through the high-level gldx::Mesh (MeshData -> Upload ->
 * Draw) and never names a VAO or a buffer; this demo deliberately does the
 * wiring Mesh hides: create the VAO and VBO, upload an interleaved float span,
 * bind the VBO inside the VAO's scope and attach each attribute by location,
 * then draw with DrawArrays. Diff the two sources and you can read exactly
 * what Mesh::Upload does on your behalf.
 *
 * Like the main target it brings *none* of the render subsystems - no
 * GeometryPass, no LightBuffer, no PBR program, no scene graph, no shadow /
 * IBL / bloom / skybox. One custom gldx::RenderPass holding an inline GLSL
 * program plus the hand-assembled VAO is the whole engine surface it uses.
 *
 * Controls: Esc quits. --quit-after SECONDS ends a scripted/headless run.
 */

// Plain text include first: it orders <glad/gl.h> before <GLFW/glfw3.h> and
// carries the GLFW/GLAD declarations this TU references directly (GL_* enums,
// GLuint, glfwSwapBuffers via gldxwin, ...). std text includes must precede
// `import gldx;`: gldx's global module fragment already attaches those entities
// to the global module and MSVC rejects a later textual re-include. The three
// libraries are then imported.
#include "gldx/core/Platform.h"

#include <cstdio>
#include <memory>
#include <span>
#include <utility>

import gldx;
import gldxwin;
import gldxcli;

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
    const gldx::cli::Flags flags(argc, argv, {}, {}, "hello_triangle");
    if (flags.wantsHelp()) {
        flags.printUsage();
        return 0;
    }

    // gldxwin brings the window up (glfwInit via the App singleton, GL 4.1 core
    // hints, makeContextCurrent, gladLoadGL). Nothing on the engine side exists
    // until this returns true.
    gldx::win::WindowDesc desc;
    desc.title = "gldx - hello triangle (manual VAO/VBO)";
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
