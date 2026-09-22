/**
 * @file main.cpp
 * @brief The smallest thing that can put pixels on screen: a hello-triangle
 *        drawn through the engine's high-level geometry resource, gldx::Mesh.
 *
 * This is the engine's "minimum implementation" demo, kept in src/ root on
 * purpose so the primary `GLFW_Template` target (and the CI artifact name that
 * hangs off it) stays stable. Its whole point is subtraction: unlike the
 * feature demos, it brings *none* of the render subsystems - no GeometryPass,
 * no LightBuffer, no PBR program, no scene graph, no shadow / IBL / bloom /
 * skybox. It defines one custom gldx::RenderPass holding an inline GLSL program
 * and a gldx::Mesh, and hands the renderer an otherwise-empty RenderFrame.
 * If this builds and draws, then "import gldx + one pass + one window" really
 * is the floor of the whole engine - and it is reached purely by importing the
 * three libraries (gldx / gldxwin / gldxcli), no demo scaffolding header.
 *
 * Mesh is the default way to get geometry on screen: fill a CPU-side MeshData,
 * Upload() once on the render thread, Draw() per frame - the VBO/VAO/EBO and
 * the fixed 5-attribute layout (see gldx/geometry/Vertex.h) stay behind that
 * door. Readers who want to see and edit that bottom layer directly - bare
 * gldx::VertexArray + gldx::GLBuffer + AttachAttribute - have the exact same
 * triangle assembled that way in src/demo/hello_triangle/.
 *
 * The window / context / frame-loop lifecycle is gldxwin's App + Window; the
 * only per-frame callback just fills fbWidth/fbHeight and renders, because the
 * triangle pass needs no data from the frame.
 *
 * Controls: Esc quits. --quit-after SECONDS ends a scripted/headless run.
 */

// Plain text includes first: Platform.h orders <glad/gl.h> before <GLFW/glfw3.h>
// and carries the GLFW/GLAD declarations this TU references directly (GL_*
// enums, GLuint, glfwSwapBuffers via gldxwin, ...). std/GLM text includes must
// precede `import gldx;`: gldx's global module fragment already attaches those
// entities to the global module and MSVC rejects a later textual re-include.
// The three libraries are then imported.
#include "gldx/core/Platform.h"

#include <cstdio>
#include <glm/glm.hpp>
#include <memory>
#include <utility>
#include <vector>

import gldx;
import gldxwin;
import gldxcli;

namespace {

// A self-contained colored triangle expressed in Mesh's fixed Vertex layout:
// position at attribute location 0 (vec3), color at location 4 (vec4) - see
// gldx/geometry/Vertex.h. `#version 410 core` matches the project's OpenGL 4.1
// baseline; no engine shader header is involved.
constexpr const char* kVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=4) in vec4 aColor;
out vec3 vColor;
void main() {
    vColor = aColor.rgb;
    gl_Position = vec4(aPos.xy, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFragment = R"GLSL(#version 410 core
in vec3 vColor;
out vec4 FragColor;
void main() { FragColor = vec4(vColor, 1.0); }
)GLSL";

// The classic first triangle, in CPU-side terms only: MeshData is pure value
// data, freely buildable anywhere; Mesh::Upload is what ships it to the GPU.
// Keeping the corner table small keeps the demo about the API, not the data.
struct Corner { float x, y; float r, g, b; };
const Corner kCorners[] = {
     0.0f,  0.6f,   1.0f, 0.35f, 0.30f,   // top, red
    -0.6f, -0.5f,   0.30f, 1.0f, 0.40f,   // bottom-left, green
     0.6f, -0.5f,   0.30f, 0.55f, 1.0f,   // bottom-right, blue
};

gldx::MeshData MakeTriangle() {
    gldx::MeshData data;
    data.vertices.reserve(3);
    for (const auto& c : kCorners) {
        gldx::Vertex v;
        v.position = glm::vec3(c.x, c.y, 0.0f);
        v.color    = glm::vec4(c.r, c.g, c.b, 1.0f);
        data.vertices.push_back(v);
    }
    return data;   // indices empty => Mesh::Draw takes the DrawArrays path
}

// One pass that owns a program + a Mesh and renders straight at the default
// framebuffer. Built on the render thread (the Window ctor has made the context
// current before main() constructs it), torn down inside Run, so both the GL
// calls here and the Mesh/ShaderProgram destructors see a live context.
class TrianglePass : public gldx::RenderPass {
public:
    TrianglePass() : RenderPass("Triangle") {
        auto program = gldx::ShaderProgram::CreateFromSource(kVertex, kFragment);
        if (!program) {
            std::fprintf(stderr, "Triangle shader error:\n%s\n", program.error().c_str());
            return;   // Execute() then becomes a no-op clear; the run still ends cleanly.
        }
        program_ = std::make_unique<gldx::ShaderProgram>(std::move(*program));

        // The whole geometry story at this level: hand the CPU data to Mesh.
        // Upload() creates the buffers + VAO and records the fixed attribute
        // layout internally - this file never names a VAO, a VBO or an
        // glVertexAttribPointer.
        mesh_.Upload(MakeTriangle());
    }

    // mesh_ is a gldx RAII wrapper: its destructor deletes the GL names on the
    // render thread (the pass is torn down inside Run, context still current).

    void Execute(gldx::RenderFrame& frame) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, frame.fbWidth, frame.fbHeight);
        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (program_) {
            program_->Use();
            mesh_.Draw();   // binds the VAO, draws the 3 vertices, unbinds
        }
    }

private:
    std::unique_ptr<gldx::ShaderProgram> program_;
    gldx::Mesh mesh_;
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
