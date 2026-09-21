/**
 * @file main.cpp
 * @brief The smallest thing that can put pixels on screen: a bare hello-triangle.
 *
 * This is the engine's "minimum implementation" demo, kept in src/ root on
 * purpose so the primary `GLFW_Template` target (and the CI artifact name that
 * hangs off it) stays stable. Its whole point is subtraction: unlike the
 * feature demos under src/demo/, it brings *none* of the render subsystems -
 * no GeometryPass, no LightBuffer, no PBR program, no scene graph, no shadow /
 * IBL / bloom / skybox. It defines one custom gfx::RenderPass holding an inline
 * GLSL program and a hand-built VAO, and hands the renderer an otherwise-empty
 * RenderFrame (Run only fills fbWidth/fbHeight). If this builds and draws, then
 * "import gfx + one pass + one window" really is the floor of the whole engine.
 *
 * The window / context / frame-loop lifecycle is demo::Run from demo_app.h; the
 * only per-frame callback does nothing, because the triangle pass needs no data
 * from the frame beyond what Run already provides.
 *
 * Controls: Esc quits. --quit-after SECONDS ends a scripted/headless run.
 */

#include "demo/demo_app.h"

#include <cstdio>
#include <memory>
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
class TrianglePass : public gfx::RenderPass {
public:
    TrianglePass() : RenderPass("Triangle") {
        auto program = gfx::ShaderProgram::CreateFromSource(kVertex, kFragment);
        if (!program) {
            std::fprintf(stderr, "Triangle shader error:\n%s\n", program.error().c_str());
            return;   // Execute() then becomes a no-op clear; the run still ends cleanly.
        }
        program_ = std::make_unique<gfx::ShaderProgram>(std::move(*program));

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
        constexpr GLsizei stride = 5 * sizeof(GLfloat);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        glBindVertexArray(0);
    }

    ~TrianglePass() override {
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vbo_) glDeleteBuffers(1, &vbo_);
    }

    void Execute(gfx::RenderFrame& frame) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, frame.fbWidth, frame.fbHeight);
        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (program_) {
            program_->Use();
            glBindVertexArray(vao_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
        }
    }

private:
    std::unique_ptr<gfx::ShaderProgram> program_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    const demo::Flags flags(argc, argv, {}, {}, "GLFW_Template");
    if (flags.wantsHelp()) {
        flags.printUsage();
        return 0;
    }

    return demo::Run(flags, "gfx - hello triangle (minimum pipeline)", [&](demo::Ctx& ctx) {
        ctx.renderer.AddPass(std::make_unique<TrianglePass>());
        // Nothing to attach: the pass reads only fb size off the frame, which
        // demo::Run already fills. An empty body is the demonstration.
        return ctx.Loop([](gfx::RenderFrame&, const demo::FrameInfo&) {});
    });
}
