// render_passes demo — one context-private view (see View.h).
#include "gldx/core/Platform.h"

#include "View.h"

#include <cstdio>
#include <memory>

#include "Passes.h"

namespace render_passes {

void View::Create() {
    // Every GL call below happens on this window's context, made current by
    // gldxwin before OnCreate. Mark this the render thread first.
    gldx::RenderContext::MarkAsRenderThread();

    renderer = std::make_unique<gldx::Renderer>();
    renderer->Init();

    // The headline of the demo: one ClearPass first so ordering is visible,
    // then a *for loop* over the factory table appending a different RenderPass
    // subclass per row. Table order == execution order.
    renderer->AddPass(std::make_unique<ClearPass>());
    for (const ShapeSpec& spec : ShapeTable()) {
        renderer->AddPass(spec.make(state, spec.cx, spec.cy, spec.r, spec.g, spec.b));
    }
}

void View::Release() {
    // Drops the renderer, which owns and destroys every pass; their programs /
    // VAOs / VBOs die with it while this window's context is still current.
    renderer.reset();
}

void View::Frame(const gldx::win::FrameInfo& info) {
    if (!info.window->ContextIsCurrent())
        std::fprintf(stderr, "render_passes view %d: context mismatch during OnFrame\n", index);
    gldx::RenderContext::AssertRenderThread("render_passes::View::Frame");
    if (!renderer) return;

    // The passes read the shared clock straight off the state_ pointer, so the
    // frame record only has to carry the framebuffer size and fps here.
    gldx::RenderFrame frame;
    frame.fbWidth     = info.fbWidth;
    frame.fbHeight    = info.fbHeight;
    frame.smoothedFps = info.smoothedFps;
    renderer->Render(frame);
}

} // namespace render_passes
