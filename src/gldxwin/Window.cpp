// ============================================================================
// gldxwin - Window implementation unit.
//
// Owns one GLFWwindow and its frame-pacing state. Everything that must happen
// while a context is current — GLAD load, the OnCreate/OnDestroy hooks — lives
// here so the ordering rule (create resources after the context, release them
// before glfwDestroyWindow) is enforced in exactly one place.
//
// gldxwin deliberately never installs a GLFW key callback and never touches the
// window user-pointer: demos own both (their mouse/scroll callbacks pull their
// own View* out of the user-pointer). Esc-to-close is therefore polled in
// App::Run via glfwGetKey, exactly as the old demo_app.h frame loop did.
// ============================================================================

module;

#include "gldxwin/gmf.hpp"

module gldxwin;

namespace gldx::win {

Window::Window(const WindowDesc& desc) : desc_(desc) {
    App::Get().Register(this);   // guarantees glfwInit ran before we create

    if (!App::Get().Ok()) return;   // GLFW never came up: stay inert

    glfwWindowHint(GLFW_RESIZABLE, desc_.resizable ? GLFW_TRUE : GLFW_FALSE);

    handle_ = glfwCreateWindow(desc_.width, desc_.height, desc_.title, nullptr, nullptr);
    if (!handle_) {
        std::fprintf(stderr, "gldxwin: failed to create window '%s' (OpenGL 4.1 core?)\n",
                     desc_.title ? desc_.title : "");
        return;
    }

    glfwMakeContextCurrent(handle_);
    glfwSwapInterval(1);   // vsync, matching the old demo_app.h behaviour

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::fprintf(stderr, "gldxwin: failed to initialize GLAD\n");
        glfwDestroyWindow(handle_);
        handle_ = nullptr;
        return;
    }
}

Window::~Window() {
    if (handle_) {
        glfwMakeContextCurrent(handle_);
        RunDestroy();   // release demo GL resources while the context is alive
        glfwDestroyWindow(handle_);
    }
    App::Get().Unregister(this);
}

bool Window::ShouldClose() const {
    return !handle_ || glfwWindowShouldClose(handle_);
}

void Window::Close() const {
    if (handle_) glfwSetWindowShouldClose(handle_, GLFW_TRUE);
}

void Window::ResetTiming(double startedAt) {
    prevTime_       = startedAt;
    fpsWindowStart_ = startedAt;
    fpsFrames_      = 0;
    smoothedFps_    = 60.0;
}

void Window::FillFrameInfo(FrameInfo& out, double startedAt, double now) {
    // FPS averaged over a fixed wall-clock window rather than an EMA of 1/dt, so
    // the HUD number matches the frame it is drawn on (same rule as Ctx::Loop).
    ++fpsFrames_;
    if (const double span = now - fpsWindowStart_; span >= 0.5) {
        smoothedFps_    = static_cast<double>(fpsFrames_) / span;
        fpsFrames_      = 0;
        fpsWindowStart_ = now;
    }

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(handle_, &fbw, &fbh);

    out.window      = this;
    out.fbWidth     = fbw;
    out.fbHeight    = fbh;
    out.time        = now - startedAt;
    out.dt          = now - prevTime_;
    out.smoothedFps = smoothedFps_;
    prevTime_       = now;
}

void Window::RunCreate() {
    if (createdHook_ || !onCreate_ || !handle_) return;
    createdHook_ = true;
    glfwMakeContextCurrent(handle_);
    onCreate_(*this);
}

void Window::RunDestroy() {
    if (onDestroy_ && handle_) onDestroy_(*this);
}

} // namespace gldx::win
