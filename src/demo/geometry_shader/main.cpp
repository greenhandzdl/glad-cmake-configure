/**
 * @file main.cpp
 * @brief geometry_shader - assemble a vertex + geometry + fragment pipeline with
 *        the selective multi-stage loader, gldx::ShaderProgram::CreateFromSources.
 *
 * ShaderProgram historically only took a vertex and a fragment source, so there
 * was no way to slot in an optional geometry (or tessellation) stage. This demo
 * exercises the generalised assembly added for that: it hands CreateFromSources
 * a three-element brace list (Vertex + Geometry + Fragment) and proves the
 * geometry stage really ran by feeding it GL_POINTS and having it expand each
 * point into a shaded square. A pipeline without the geometry stage could not
 * draw those squares at all, so "exit 0 + a snapshot full of squares" is direct
 * evidence the extra stage was compiled, attached and linked — not just accepted
 * by the API.
 *
 * --snapshot PATH       dump the framebuffer once at --snap-at SECONDS.
 * --snap-at SECONDS     when to take the snapshot (default 2).
 * --quit-after SECONDS  headless auto-close.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <cstdio>
#include <span>
#include <string>
#include <vector>

namespace {

// The three pipeline stages. Only the vertex and fragment are the classic pair;
// the geometry shader is the stage this demo exists to prove the loader can now
// assemble selectively.
constexpr const char* kVert = R"GLSL(#version 410 core
layout(location = 0) in vec2 aPos;   // NDC centre of one square
out vec2 vCenter;
void main() {
    vCenter = aPos;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kGeom = R"GLSL(#version 410 core
layout(points) in;                     // one vertex per point
layout(triangle_strip, max_vertices = 4) out;
in vec2 vCenter[];
out vec2 vLocal;                       // -1..1 within the square, for shading
uniform float uHalf;                   // half-size in NDC
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

// The GL objects this demo owns: an empty-ish point buffer, its VAO, and the
// three-stage program. Constructed empty on the main thread, filled in OnCreate
// (render thread, context current), destroyed at end of main before the Window
// and long before glfwTerminate — the same teardown order as every other demo.
struct State {
    gldx::ShaderProgram program;
    gldx::VertexArray   vao;
    gldx::GLBuffer      vbo;
    GLsizei             vertexCount = 0;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"snapshot", "snap-at"}, "geometry_shader");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const std::string snapPath = flags.string("snapshot");
    const double      snapAt   = flags.number("snap-at", 2.0);

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - geometry_shader (ShaderProgram::CreateFromSources)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    State st;
    int  exitCode = 0;
    bool snapped  = false;

    window.OnCreate([&](gldx::win::Window& w) {
        gldx::RenderContext::MarkAsRenderThread();

        // The selective assembly: a vertex and fragment are mandatory, the
        // geometry stage is the optional extra added by listing it.
        auto loaded = gldx::ShaderProgram::CreateFromSources({
            {gldx::ShaderStage::Vertex,   kVert},
            {gldx::ShaderStage::Geometry, kGeom},
            {gldx::ShaderStage::Fragment, kFrag},
        });
        if (!loaded) {
            std::fprintf(stderr, "geometry_shader: CreateFromSources(V+Geom+F) failed: %s\n",
                         loaded.error().c_str());
            exitCode = 1;
            w.Close();
            return;
        }
        std::printf("geometry_shader: assembled a 3-stage program (vertex + geometry + fragment)\n");
        st.program = std::move(*loaded);

        // A small grid of point centres; the geometry shader turns each into a
        // square, so what you see is 12 quads, not 12 dots.
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
                std::printf("geometry_shader: captured %s\n", snapPath.c_str());
            else
                std::fprintf(stderr, "geometry_shader: snapshot failed: %s\n", snapPath.c_str());
            info.window->Close();
        }
    });

    const int rc = gldx::win::App::Get().Run({flags.quitAfter()});
    return exitCode ? exitCode : rc;
}
