/**
 * @file main.cpp
 * @brief shader_stages - one program with every 4.1 graphics stage assembled
 *        through gldx::ShaderProgram::CreateFromSources.
 *
 * The point of this demo is breadth of the selective loader: a single brace
 * list hands CreateFromSources all five available graphics stages — vertex,
 * tessellation control, tessellation evaluation, geometry and fragment — and it
 * compiles, attaches and links them into one program. The GLSL lives in
 * Stages.h; this file is only the wiring and the draw.
 *
 * The draw itself is the proof: four control points are submitted as one
 * GL_PATCHES quad, the tessellation pair subdivides it, and the geometry stage
 * re-emits every generated triangle as a wireframe line strip. A snapshot full
 * of an animated subdivided lattice cannot be produced unless all five stages
 * are live, so a clean exit plus such a screenshot verifies the whole pipeline.
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

#include "Stages.h"

namespace {

// The four patch control points: a unit-ish quad in NDC, counter-clockwise so it
// matches the evaluation stage's `ccw` winding. Order maps to the bilinear
// weights tc[0..3] used in Stages.h.
constexpr float kQuadCorners[] = {
    -0.7f, -0.7f,   // 0: (u=0,v=0)
     0.7f, -0.7f,   // 1: (u=1,v=0)
     0.7f,  0.7f,   // 2: (u=1,v=1)
    -0.7f,  0.7f,   // 3: (u=0,v=1)
};
constexpr GLsizei kPatchVertices = 4;

struct State {
    gldx::ShaderProgram program;
    gldx::VertexArray   vao;
    gldx::GLBuffer      vbo;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"snapshot", "snap-at"}, "shader_stages");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const std::string snapPath = flags.string("snapshot");
    const double      snapAt   = flags.number("snap-at", 2.0);

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - shader_stages (all 5 pipeline stages)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    State st;
    int  exitCode = 0;
    bool snapped  = false;

    window.OnCreate([&](gldx::win::Window& w) {
        gldx::RenderContext::MarkAsRenderThread();

        // The whole demo in one call: hand the loader all five graphics stages.
        auto loaded = gldx::ShaderProgram::CreateFromSources({
            {gldx::ShaderStage::Vertex,         shader_stages::kVertex},
            {gldx::ShaderStage::TessControl,    shader_stages::kTessControl},
            {gldx::ShaderStage::TessEvaluation, shader_stages::kTessEval},
            {gldx::ShaderStage::Geometry,       shader_stages::kGeometry},
            {gldx::ShaderStage::Fragment,       shader_stages::kFragment},
        });
        if (!loaded) {
            std::fprintf(stderr, "shader_stages: CreateFromSources(5 stages) failed: %s\n",
                         loaded.error().c_str());
            exitCode = 1;
            w.Close();
            return;
        }
        std::printf("shader_stages: assembled a 5-stage program "
                    "(vertex + tess control + tess eval + geometry + fragment)\n");
        st.program = std::move(*loaded);

        st.vbo.Create(GL_ARRAY_BUFFER, std::span<const float>(kQuadCorners));
        st.vao.Create();
        st.vao.Bind();
        st.vbo.Bind(GL_ARRAY_BUFFER);
        st.vao.AttachAttribute(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        st.vao.Unbind();
    });

    window.OnFrame([&](gldx::win::FrameInfo& info) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, info.fbWidth, info.fbHeight);
        glClearColor(0.04f, 0.05f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (st.program.valid()) {
            const float aspect = info.fbHeight > 0
                                     ? static_cast<float>(info.fbWidth) / static_cast<float>(info.fbHeight)
                                     : 1.0f;
            st.program.Use();
            st.program.Set("uPhase",  static_cast<float>(gldx::win::App::Now()));
            st.program.Set("uAspect", aspect);
            // Patch configuration is pass-level GL, deliberately called raw.
            glPatchParameteri(GL_PATCH_VERTICES, kPatchVertices);
            st.vao.Bind();
            st.vao.DrawArrays(GL_PATCHES, 0, kPatchVertices);
            st.vao.Unbind();
        }

        if (!snapPath.empty() && !snapped && info.time >= snapAt) {
            snapped = true;
            if (info.window->CaptureScreenshot(snapPath))
                std::printf("shader_stages: captured %s\n", snapPath.c_str());
            else
                std::fprintf(stderr, "shader_stages: snapshot failed: %s\n", snapPath.c_str());
            info.window->Close();
        }
    });

    const int rc = gldx::win::App::Get().Run({flags.quitAfter()});
    return exitCode ? exitCode : rc;
}
