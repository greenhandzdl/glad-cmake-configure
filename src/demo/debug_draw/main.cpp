/**
 * @file main.cpp
 * @brief debug_draw - an immediate-mode line overlay + CPU/GPU frame timing.
 *
 * DebugDraw accumulates coloured line segments on the CPU between Clear() and
 * Draw(viewProj), then submits them in one GL_LINES call with the depth test off,
 * so diagnostic geometry (axes, bounding boxes, selection outlines) reads on top
 * of whatever is behind it. It owns a single shader + VAO + streaming VBO of
 * fixed capacity; overflow is dropped, because this is debug output not worth
 * reallocating mid-frame.
 *
 * Profiler brackets a frame's GL work with a GL_TIME_ELAPSED query plus a CPU
 * clock snapshot (gl_ARB_timer_query is core since 3.3, so the 4.1 baseline has
 * it). GPU elapsed time lands one frame late - normal for async timer queries,
 * and the read-back of the previous frame happens inside BeginFrame so there is
 * never a synchronous stall. Here the profiler wraps just this pass's own draw,
 * which is enough to show a live non-zero timing without pulling in the rest of
 * the pipeline.
 *
 * The overlay alone would be invisible without a projection to share, so a slow
 * auto-orbit supplies viewProj. A tiny text readout echoes the timings.
 *
 * Controls: Esc quits, --quit-after SECONDS for headless.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <cmath>
#include <cstdio>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1;
    d.channels = 4;
    d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

class DebugPass : public gldx::RenderPass {
public:
    DebugPass() : RenderPass("Debug") {
        if (!debug_.Init()) std::fprintf(stderr, "DebugDraw init failed\n");
        profiler_.Init();
        if (!sprite_.Init()) std::fprintf(stderr, "SpriteBatch init failed\n");
        for (const char* candidate : kFontCandidates) {
            if (font_.LoadFromFile(candidate, 48.0f)) break;
        }
        white_.Upload(MakeSolidDesc());
    }

    void Execute(gldx::RenderFrame& f) override {
        profiler_.BeginFrame();

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!f.camera) { profiler_.EndFrame(); return; }

        // Rebuild the overlay each frame: world axes at the origin, a ground grid
        // of wire boxes, and one box scaled + spun to show the push helpers work.
        debug_.Clear();
        debug_.PushAxes(glm::vec3(0.0f), 2.0f);
        const float t = static_cast<float>(info_.time);
        for (int ix = -3; ix <= 3; ++ix) {
            for (int iz = -3; iz <= 3; ++iz) {
                const glm::vec3 c(static_cast<float>(ix) * 1.5f,
                                  0.5f + 0.4f * std::sin(t + static_cast<float>(ix) + static_cast<float>(iz)),
                                  static_cast<float>(iz) * 1.5f);
                const glm::vec3 half(0.4f);
                const glm::vec4 col(0.2f + 0.1f * static_cast<float>(ix + 3),
                                    0.55f, 0.9f - 0.1f * static_cast<float>(iz + 3), 0.6f);
                debug_.PushBoxCenter(c, half, col);
            }
        }
        debug_.PushBox(glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(1.0f, 2.5f, 1.0f),
                       glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
        debug_.Draw(f.viewProj);

        if (font_.loaded() && white_.valid()) {
            char line[128];
            std::snprintf(line, sizeof(line),
                          "CPU %.2f ms   GPU %.2f ms   lines %zu",
                          profiler_.CpuMs(), profiler_.GpuMs(), debug_.lineCount());
            sprite_.Begin(white_, f.fbWidth, f.fbHeight);
            sprite_.Draw(white_, 14.0f, 14.0f, 360.0f, 40.0f,
                         0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.4f));
            gldx::TextRenderer::Draw(sprite_, font_, line, 24.0f, 22.0f, 20.0f,
                                    glm::vec4(0.75f, 0.9f, 1.0f, 1.0f));
            sprite_.End();
        }

        profiler_.EndFrame();
    }

    void setFrameInfo(gldx::win::FrameInfo info) noexcept { info_ = info; }

private:
    gldx::DebugDraw   debug_;
    gldx::Profiler    profiler_;
    gldx::SpriteBatch sprite_;
    gldx::Font        font_;
    gldx::Texture2D   white_;
    gldx::win::FrameInfo info_;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {}, "debug_draw");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - debug_draw (line overlay + profiler)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();

    auto pass = std::make_unique<DebugPass>();
    DebugPass* raw = pass.get();
    renderer.AddPass(std::move(pass));

    gldx::Camera camera; camera.SetPerspective(50.0f, 1.0f, 0.1f, 100.0f);

    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        const float yaw = static_cast<float>(info.time) * 0.3f;
        const glm::vec3 eye(8.0f * std::cos(yaw), 6.0f, 8.0f * std::sin(yaw));
        camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
        camera.LookAt(eye, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0, 1, 0));

        raw->setFrameInfo(info);
        f.camera = &camera;
        f.viewProj = camera.ViewProjection();

        renderer.Render(f);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
