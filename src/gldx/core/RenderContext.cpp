module;

#include "gldx/gmf.hpp"

#include <cstdio>
#include <cstdlib>

module gldx;

namespace gldx {

namespace {
// Thread-local flag: exactly true on the thread that called MarkAsRenderThread().
thread_local bool g_isRenderThread = false;
} // namespace

void RenderContext::MarkAsRenderThread() noexcept {
    g_isRenderThread = true;
}

bool RenderContext::IsRenderThread() noexcept {
    return g_isRenderThread;
}

void RenderContext::AssertRenderThread(const char* where) noexcept {
    if (!g_isRenderThread) {
        std::fprintf(stderr,
            "[gldx::RenderContext] FATAL: GL call from non-render thread (%s)\n",
            where ? where : "?");
        std::abort();
    }
}

} // namespace gldx
