// ============================================================================
// gldxwin - App implementation unit.
//
// The process-wide GLFW owner and the multi-window frame loop. Run() reproduces
// the exact per-frame bookkeeping the old demo::Ctx::Loop did (elapsed time, dt,
// the 0.5s-window FPS average, Esc-to-close, --quit-after) but generalized to
// drive every live window in one pass, then hands each its FrameInfo and swaps.
// ============================================================================

module;

#include "gldxwin/gmf.hpp"

module gldxwin;

namespace gldx::win {

App& App::Get() {
    static App instance;
    return instance;
}

App::App() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(GLFW_PLATFORM_MACOS) && GLFW_PLATFORM_MACOS
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    // GLFW_PLATFORM_MACOS comes from gldx/core/Platform.h, which importers
    // textually include; when gldxwin is compiled standalone it is absent, so
    // fall back to the Apple detector GLFW itself uses.
#if !defined(GLFW_PLATFORM_MACOS) && defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    if (!glfwInit()) {
        std::fprintf(stderr, "gldxwin: failed to initialize GLFW\n");
        ok_ = false;
        return;
    }
    ok_ = true;
}

App::~App() {
    // Runs after main()'s Window objects are gone (function-local static), so
    // glfwTerminate sees no live windows and no demo is touching a dead context.
    if (ok_) glfwTerminate();
}

void App::Register(Window* window) {
    windows_.push_back(window);
}

void App::Unregister(Window* window) {
    for (auto it = windows_.begin(); it != windows_.end(); ++it)
        if (*it == window) { windows_.erase(it); break; }
}

bool App::AnyWindowOpen() const {
    for (const Window* w : windows_)
        if (w->Ok() && !w->ShouldClose()) return true;
    return false;
}

int App::Run(const RunOptions& options) {
    if (!ok_) return 1;

    const double startedAt = glfwGetTime();
    for (Window* w : windows_) w->ResetTiming(startedAt);

    // Fire OnCreate once for each window that has one, before the first frame,
    // with that window's context current (the demo builds GL resources here).
    for (Window* w : windows_)
        if (w->Ok()) w->RunCreate();

    while (AnyWindowOpen()) {
        glfwPollEvents();
        const double now = glfwGetTime();

        for (Window* w : windows_) {
            if (!w->Ok() || w->ShouldClose()) continue;

            if (w->closeOnEsc_ && glfwGetKey(w->handle_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                w->Close();
            if (options.quitAfterSeconds > 0.0 && (now - startedAt) >= options.quitAfterSeconds)
                w->Close();

            // The frame that *triggers* the close is still drawn and swapped
            // here; AnyWindowOpen() ends the loop on the next pass. This matches
            // the old Ctx::Loop, where should-close was only re-tested at the
            // top of the while, so the closing frame was never skipped.
            FrameInfo info;
            w->FillFrameInfo(info, startedAt, now);
            if (w->onFrame_) w->onFrame_(info);

            glfwSwapBuffers(w->handle_);
        }
    }
    return 0;
}

} // namespace gldx::win
