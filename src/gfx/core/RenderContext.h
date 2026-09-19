#ifndef GFX_CORE_RENDER_CONTEXT_H
#define GFX_CORE_RENDER_CONTEXT_H

/**
 * @file RenderContext.h
 * @brief GL-context thread affinity guard.
 *
 * OpenGL contexts are thread-bound: only one thread may have the context
 * current at a time and every gl* call must happen on that thread. Phase 1
 * uses a single render thread; this helper records which thread owns the
 * context so any GL-touching class can assert it (see plan section 1.1).
 */

#include <thread>

namespace gfx {

class RenderContext {
public:
    // Bind the calling thread as the render thread. Called once on the
    // main thread right after glfwMakeContextCurrent().
    static void MarkAsRenderThread() noexcept;

    // True when the caller is the render thread.
    [[nodiscard]] static bool IsRenderThread() noexcept;

    // Debug assertion helper: aborts (with a message) if not on render thread.
    static void AssertRenderThread(const char* where = nullptr) noexcept;
};

} // namespace gfx

#endif // GFX_CORE_RENDER_CONTEXT_H
