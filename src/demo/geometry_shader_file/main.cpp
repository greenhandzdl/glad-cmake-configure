/**
 * @file main.cpp
 * @brief geometry_shader_file - assemble a vertex + geometry + fragment pipeline
 *        entirely from on-disk GLSL, via the multi-stage file loader
 *        gldx::ShaderProgram::CreateFromFiles(std::initializer_list<ShaderFile>).
 *
 * shader_file proves the two-path file loader (vertex + fragment); geometry_shader
 * proves the selective multi-stage loader from *embedded* strings. The entry point
 * left undemonstrated was their combination: loading a program that has an
 * optional geometry (or tessellation) stage from *files*. That is what this demo
 * covers - it hands CreateFromFiles a three-element brace list of {stage, path}
 * pairs and links a program whose geometry stage came off disk.
 *
 * The draw is the proof, mirroring geometry_shader: a grid of GL_POINTS is fed
 * in and the geometry stage expands each into a shaded square. A program without
 * a working geometry stage could not draw those squares, so "exit 0 + a snapshot
 * full of squares" is direct evidence the file-backed multi-stage assembly
 * compiled, linked and ran - not just that two files were read.
 *
 * --vert PATH --geom PATH --frag PATH
 *     the three GLSL files to load (run from the repo root and point at
 *     src/demo/geometry_shader_file/assets/shaders/points.vert / squares.geom / points.frag).
 *     All three must be given to take the file path; a given set that fails to
 *     load is a hard error (exit 1), so a scripted check trusts "exit 0 +
 *     snapshot" as proof. With none given the demo falls back to an identical
 *     *embedded* 3-stage source so a bare run still opens a window (CI-safe).
 * --snapshot PATH       dump the framebuffer once at --snap-at SECONDS.
 * --snap-at SECONDS     when to take the snapshot (default 2).
 * --quit-after SECONDS  headless auto-close.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <cstdio>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace {

// Embedded twins of the three on-disk stages (see assets/shaders/), used only
// when no --vert/--geom/--frag was passed. Effect-identical to the files so a
// fallback run and a file run are visually indistinguishable; the fallback goes
// through the embedded
// multi-stage loader (CreateFromSources) to keep the two paths clearly distinct.
constexpr const char* kVert = R"GLSL(#version 410 core
layout(location = 0) in vec2 aPos;
out vec2 vCenter;
void main() {
    vCenter = aPos;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kGeom = R"GLSL(#version 410 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec2 vCenter[];
out vec2 vLocal;
uniform float uHalf;
void main() {
    vec2 corners[4] = vec2[4](
        vec2(-1.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0,  1.0), vec2(1.0,  1.0));
    for (int i = 0; i < 4; ++i) {
        vLocal = corners[i];
        gl_Position = vec4(vCenter[0] + corners[i] * uHalf, 0.0, 1.0);
        EmitVertex();
    }
    EndPrimitive();
}
)GLSL";

constexpr const char* kFrag = R"GLSL(#version 410 core
in vec2 vLocal;
out vec4 FragColor;
void main() {
    float d = clamp(1.0 - length(vLocal), 0.0, 1.0);
    vec3 base = vec3(0.18, 0.45, 0.75);
    FragColor = vec4(base + 0.45 * d, 1.0);
}
)GLSL";

// The GL objects this demo owns: a point-centre buffer, its VAO, and the
// three-stage program. Filled in OnCreate (render thread, context current),
// destroyed at end of main before the Window and long before glfwTerminate.
struct State {
    gldx::ShaderProgram program;
    gldx::VertexArray   vao;
    gldx::GLBuffer      vbo;
    GLsizei             vertexCount = 0;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {},
                                 {"vert", "geom", "frag", "snapshot", "snap-at"},
                                 "geometry_shader_file");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const std::string vert     = flags.string("vert");
    const std::string geom     = flags.string("geom");
    const std::string frag     = flags.string("frag");
    const std::string snapPath = flags.string("snapshot");
    const double      snapAt   = flags.number("snap-at", 2.0);
    const bool        wantFiles = !vert.empty() && !geom.empty() && !frag.empty();

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - geometry_shader_file (CreateFromFiles, multi-stage)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    State st;
    int  exitCode = 0;
    bool snapped  = false;

    window.OnCreate([&](gldx::win::Window& w) {
        gldx::RenderContext::MarkAsRenderThread();

        std::expected<gldx::ShaderProgram, std::string> loaded =
            [&]() -> std::expected<gldx::ShaderProgram, std::string> {
            if (wantFiles) {
                // The multi-stage file loader: a {stage, path} per stage, order
                // irrelevant, each stage at most once. Reads all three off disk.
                auto prog = gldx::ShaderProgram::CreateFromFiles({
                    {gldx::ShaderStage::Vertex,   vert},
                    {gldx::ShaderStage::Geometry, geom},
                    {gldx::ShaderStage::Fragment, frag},
                });
                if (prog)
                    std::printf("geometry_shader_file: assembled a 3-stage program from "
                                "FILES: %s + %s + %s\n",
                                vert.c_str(), geom.c_str(), frag.c_str());
                return prog;
            }
            std::printf("geometry_shader_file: no --vert/--geom/--frag, "
                        "using embedded fallback source\n");
            return gldx::ShaderProgram::CreateFromSources({
                {gldx::ShaderStage::Vertex,   kVert},
                {gldx::ShaderStage::Geometry, kGeom},
                {gldx::ShaderStage::Fragment, kFrag},
            });
        }();

        if (!loaded) {
            std::fprintf(stderr, "geometry_shader_file: assembly failed: %s\n",
                         loaded.error().c_str());
            exitCode = 1;
            w.Close();
            return;
        }
        st.program = std::move(*loaded);

        // A small grid of point centres; the (file-loaded) geometry shader turns
        // each into a square, so what you see is 12 quads, not 12 dots.
        std::vector<float> pts;
        const float cols[] = {-0.6f, -0.2f, 0.2f, 0.6f};
        const float rows[] = {-0.45f, 0.0f, 0.45f};
        for (float y : rows)
            for (float x : cols) { pts.push_back(x); pts.push_back(y); }
        st.vertexCount = static_cast<GLsizei>(pts.size() / 2);

        st.vbo.Create(GL_ARRAY_BUFFER, std::span<const float>(pts));
        st.vao.Create();
        st.vao.Bind();
        st.vbo.Bind(GL_ARRAY_BUFFER);
        st.vao.AttachAttribute(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        st.vao.Unbind();
    });

    window.OnFrame([&](gldx::win::FrameInfo& info) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, info.fbWidth, info.fbHeight);
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (st.program.valid()) {
            st.program.Use();
            st.program.Set("uHalf", 0.14f);
            st.vao.Bind();
            st.vao.DrawArrays(GL_POINTS, 0, st.vertexCount);
            st.vao.Unbind();
        }

        if (!snapPath.empty() && !snapped && info.time >= snapAt) {
            snapped = true;
            if (info.window->CaptureScreenshot(snapPath))
                std::printf("geometry_shader_file: captured %s\n", snapPath.c_str());
            else
                std::fprintf(stderr, "geometry_shader_file: snapshot failed: %s\n", snapPath.c_str());
            info.window->Close();
        }
    });

    const int rc = gldx::win::App::Get().Run({flags.quitAfter()});
    return exitCode ? exitCode : rc;
}
