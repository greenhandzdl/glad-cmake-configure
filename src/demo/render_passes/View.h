#ifndef RENDER_PASSES_VIEW_H
#define RENDER_PASSES_VIEW_H

/**
 * @file View.h
 * @brief One window's context-private renderer for the render_passes demo.
 *
 * Each window owns a gldx::Renderer plus a chain of RenderPass objects that
 * live only on that window's GL context. Create() runs inside the window's
 * OnCreate (its context current), where the shape passes compile their own
 * programs and upload their own VAO/VBO; Release() runs inside OnDestroy while
 * the same context is still current. Nothing here is shared GPU-side between
 * windows - the only cross-window truth is the CPU atomics in SharedState.h
 * that every pass samples once per frame, which is what keeps the shapes
 * spinning in lockstep across every context.
 *
 * Create() is also where the demo's headline lives: it AddPass()es a ClearPass
 * and then loops render_passes::ShapeTable(), so a *different* RenderPass
 * subclass (triangle / quad / lines / points / a second triangle) is appended
 * per table row - the "one for loop, many render-pass subclasses" wiring.
 */

#include <memory>

import gldx;
import gldxwin;

#include "SharedState.h"

namespace render_passes {

struct View {
    int          index = 0;
    int          total = 1;
    SharedState* state = nullptr;

    std::unique_ptr<gldx::Renderer> renderer;

    // Build this window's private pass chain on its own context. Called from
    // OnCreate with the context already current.
    void Create();
    // Free the context-private renderer; called from OnDestroy, context current.
    void Release();
    // Sample the shared clock, fill a RenderFrame, run the pass chain.
    void Frame(const gldx::win::FrameInfo& info);
};

} // namespace render_passes

#endif // RENDER_PASSES_VIEW_H
