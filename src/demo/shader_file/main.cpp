/**
 * @file main.cpp
 * @brief shader_file - load + link a GLSL program from .vert / .frag on disk.
 *
 * The engine's own shaders are embedded raw strings, the single source of truth
 * living in the src/gldx/shader "*Shaders.h" headers. This demo exercises the
 * opt-in external loader gldx::ShaderProgram::CreateFromFiles: it reads a vertex
 * and a fragment file
 * from the assets tree and builds a program at runtime, then proves the program
 * really ran by drawing a full-screen triangle straight from gl_VertexID (no
 * vertex buffer at all) shaded with a warm diagonal gradient - something no
 * clear colour could fake.
 *
 * --vert PATH --frag PATH  the two GLSL files to load (run it from the repo
 *     root and point at src/assets/shaders/file_demo/triangle.{vert,frag}).
 *     If neither is given the demo falls back to an identical embedded source so
 *     a bare run still opens a window; a *given* path that fails to load is a
 *     hard error (exit 1), so a scripted check can trust "exit 0 + snapshot" as
 *     proof the file path truly compiled, linked and drew.
 * --snapshot PATH          dump the framebuffer once at --snap-at SECONDS.
 * --snap-at SECONDS        when to take the snapshot (default 2).
 * --quit-after SECONDS     headless auto-close.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <cstdio>
#include <string>

namespace {

// Embedded twin of the file_demo shaders, used only when no --vert/--frag was
// passed. Kept effect-identical to the files so a fallback run and a file run
// are visually indistinguishable.
constexpr const char* kFallbackVert = R"GLSL(#version 410 core
out vec2 vUV;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFallbackFrag = R"GLSL(#version 410 core
in vec2 vUV;
out vec4 FragColor;
void main() {
    float t = clamp(vUV.x + vUV.y, 0.0, 1.0);
    vec3 c = mix(vec3(0.10, 0.40, 0.80), vec3(0.90, 0.50, 0.20), t);
    FragColor = vec4(c, 1.0);
}
)GLSL";

// The GL objects this demo owns. Constructed empty on the main thread, filled in
// OnCreate (context current, render thread), and destroyed at end of main -
// before the Window (declared earlier, so destroyed later) and long before
// glfwTerminate, matching every other demo's teardown order.
struct State {
    gldx::ShaderProgram program;
    gldx::VertexArray   vao;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {},
                                 {"vert", "frag", "snapshot", "snap-at"}, "shader_file");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const std::string vert     = flags.string("vert");
    const std::string frag     = flags.string("frag");
    const std::string snapPath = flags.string("snapshot");
    const double      snapAt   = flags.number("snap-at", 2.0);
    const bool        wantFiles = !vert.empty() && !frag.empty();

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - shader_file (ShaderProgram::CreateFromFiles)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    State st;
    int  exitCode = 0;
    bool snapped  = false;

    window.OnCreate([&](gldx::win::Window& w) {
        gldx::RenderContext::MarkAsRenderThread();

        if (wantFiles) {
            auto loaded = gldx::ShaderProgram::CreateFromFiles(vert, frag);
            if (!loaded) {
                std::fprintf(stderr, "shader_file: CreateFromFiles(%s, %s) failed: %s\n",
                             vert.c_str(), frag.c_str(), loaded.error().c_str());
                exitCode = 1;
                w.Close();
                return;
            }
            std::printf("shader_file: loaded program from FILES: %s + %s\n",
                        vert.c_str(), frag.c_str());
            st.program = std::move(*loaded);
        } else {
            std::printf("shader_file: no --vert/--frag, using embedded fallback source\n");
            auto loaded = gldx::ShaderProgram::CreateFromSource(kFallbackVert, kFallbackFrag);
            if (!loaded) {
                std::fprintf(stderr, "shader_file: fallback compile failed: %s\n",
                             loaded.error().c_str());
                exitCode = 1;
                w.Close();
                return;
            }
            st.program = std::move(*loaded);
        }

        st.vao.Create();   // empty VAO is enough for a gl_VertexID-driven triangle
    });

    window.OnFrame([&](gldx::win::FrameInfo& info) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, info.fbWidth, info.fbHeight);
        glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (st.program.valid()) {
            st.program.Use();
            st.vao.Bind();
            st.vao.DrawArrays(GL_TRIANGLES, 0, 3);
            st.vao.Unbind();
        }

        if (!snapPath.empty() && !snapped && info.time >= snapAt) {
            snapped = true;
            if (info.window->CaptureScreenshot(snapPath))
                std::printf("shader_file: captured %s\n", snapPath.c_str());
            else
                std::fprintf(stderr, "shader_file: snapshot failed: %s\n", snapPath.c_str());
            info.window->Close();
        }
    });

    const int rc = gldx::win::App::Get().Run({flags.quitAfter()});
    return exitCode ? exitCode : rc;
}
