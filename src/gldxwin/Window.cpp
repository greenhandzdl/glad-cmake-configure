// ============================================================================
// gldxwin - Window implementation unit.
//
// Owns one GLFWwindow and its frame-pacing state. Everything that must happen
// while a context is current — GLAD load, the OnCreate/OnDestroy hooks — lives
// here so the ordering rule (create resources after the context, release them
// before glfwDestroyWindow) is enforced in exactly one place.
//
// This unit is also the *only* place in the project where window-system input
// names appear: the GLFW callbacks are installed here behind the portable
// OnKey/OnChar/OnMouseButton/OnCursor/OnScroll surface, and the GLFW_* key /
// button constants are translated to (from) gldx::win::Key / MouseButton in the
// mapping tables below. The GLFW window user-pointer is reserved to point back
// at the Window for the trampolines; the demo-facing user data lives in
// userData_ instead. Callbacks are cleared in the destructor before
// glfwDestroyWindow, because GLFW asserts if a mouse callback is still set at
// window destruction.
// ============================================================================

module;

#include "gldxwin/gmf.hpp"

module gldxwin;

namespace gldx::win {

namespace {

// ---- portable <-> GLFW key mapping ------------------------------------------
// The enum is dense, so a flat array indexed by Key suffices; Unknown doubles
// as the out-of-range fallback.
int ToGlfw(Key key) {
    static constexpr int kMap[] = {
        GLFW_KEY_UNKNOWN,
        GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E, GLFW_KEY_F,
        GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
        GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_O, GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R,
        GLFW_KEY_S, GLFW_KEY_T, GLFW_KEY_U, GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X,
        GLFW_KEY_Y, GLFW_KEY_Z,
        GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
        GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
        GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_SPACE, GLFW_KEY_ENTER,
        GLFW_KEY_BACKSPACE,
        GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN,
        GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_MINUS,
        GLFW_KEY_EQUAL, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_SHIFT,
    };
    const auto i = static_cast<int>(key);
    return (i >= 0 && i < static_cast<int>(sizeof(kMap) / sizeof(kMap[0]))) ? kMap[i]
                                                                           : GLFW_KEY_UNKNOWN;
}

Key FromGlfw(int glfwKey) {
    static constexpr int kMap[] = {
        GLFW_KEY_UNKNOWN,
        GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E, GLFW_KEY_F,
        GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
        GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_O, GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R,
        GLFW_KEY_S, GLFW_KEY_T, GLFW_KEY_U, GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X,
        GLFW_KEY_Y, GLFW_KEY_Z,
        GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_5,
        GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
        GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_SPACE, GLFW_KEY_ENTER,
        GLFW_KEY_BACKSPACE,
        GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN,
        GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_MINUS,
        GLFW_KEY_EQUAL, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_LEFT_SHIFT,
    };
    for (int i = 0; i < static_cast<int>(sizeof(kMap) / sizeof(kMap[0])); ++i)
        if (kMap[i] == glfwKey) return static_cast<Key>(i);
    return Key::Unknown;
}

int ToGlfw(MouseButton button) {
    switch (button) {
        case MouseButton::Left:   return GLFW_MOUSE_BUTTON_LEFT;
        case MouseButton::Right:  return GLFW_MOUSE_BUTTON_RIGHT;
        case MouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
    }
    return GLFW_MOUSE_BUTTON_LEFT;
}

MouseButton FromGlfwButton(int glfwButton) {
    if (glfwButton == GLFW_MOUSE_BUTTON_RIGHT)  return MouseButton::Right;
    if (glfwButton == GLFW_MOUSE_BUTTON_MIDDLE) return MouseButton::Middle;
    return MouseButton::Left;
}

KeyAction ToAction(int glfwAction) {
    if (glfwAction == GLFW_RELEASE) return KeyAction::Release;
    if (glfwAction == GLFW_REPEAT)  return KeyAction::Repeat;
    return KeyAction::Press;
}

} // namespace

// The trampolines are static members (declared in the class), so they may touch
// the private on*_ callbacks of whatever Window the user-pointer names.
void Window::TrampKey(GLFWwindow* w, int key, int, int action, int mods) {
    if (Window* self = static_cast<Window*>(glfwGetWindowUserPointer(w)))
        if (self->onKey_) self->onKey_(*self, FromGlfw(key), ToAction(action), mods);
}

void Window::TrampChar(GLFWwindow* w, unsigned int codepoint) {
    if (Window* self = static_cast<Window*>(glfwGetWindowUserPointer(w)))
        if (self->onChar_) self->onChar_(*self, codepoint);
}

void Window::TrampMouseButton(GLFWwindow* w, int button, int action, int mods) {
    if (Window* self = static_cast<Window*>(glfwGetWindowUserPointer(w)))
        if (self->onButton_)
            self->onButton_(*self, FromGlfwButton(button), ToAction(action), mods);
}

void Window::TrampCursor(GLFWwindow* w, double x, double y) {
    if (Window* self = static_cast<Window*>(glfwGetWindowUserPointer(w)))
        if (self->onCursor_) self->onCursor_(*self, Vec2d{x, y});
}

void Window::TrampScroll(GLFWwindow* w, double dx, double dy) {
    if (Window* self = static_cast<Window*>(glfwGetWindowUserPointer(w)))
        if (self->onScroll_) self->onScroll_(*self, dx, dy);
}

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

    // The trampolines are installed unconditionally: they cost one null check
    // per event while no callback is set, and in exchange a demo can subscribe
    // at any time without touching GLFW. The user-pointer is the trampoline's
    // route back to this Window; it is cleared again in ~Window.
    glfwSetWindowUserPointer(handle_, this);
    glfwSetKeyCallback(handle_, Window::TrampKey);
    glfwSetCharCallback(handle_, Window::TrampChar);
    glfwSetMouseButtonCallback(handle_, Window::TrampMouseButton);
    glfwSetCursorPosCallback(handle_, Window::TrampCursor);
    glfwSetScrollCallback(handle_, Window::TrampScroll);

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
        // GLFW requires the mouse callbacks to be unset before destruction, and
        // clearing the user-pointer guarantees no trampoline can fire again.
        glfwSetKeyCallback(handle_, nullptr);
        glfwSetCharCallback(handle_, nullptr);
        glfwSetMouseButtonCallback(handle_, nullptr);
        glfwSetCursorPosCallback(handle_, nullptr);
        glfwSetScrollCallback(handle_, nullptr);
        glfwSetWindowUserPointer(handle_, nullptr);
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

// ---- input subscriptions ----------------------------------------------------

void Window::OnKey(std::function<void(Window&, Key, KeyAction, int mods)> cb) {
    onKey_ = std::move(cb);
}
void Window::OnChar(std::function<void(Window&, unsigned int codepoint)> cb) {
    onChar_ = std::move(cb);
}
void Window::OnMouseButton(
    std::function<void(Window&, MouseButton, KeyAction, int mods)> cb) {
    onButton_ = std::move(cb);
}
void Window::OnCursor(std::function<void(Window&, Vec2d)> cb) {
    onCursor_ = std::move(cb);
}
void Window::OnScroll(std::function<void(Window&, double dx, double dy)> cb) {
    onScroll_ = std::move(cb);
}

// ---- polled input state -----------------------------------------------------

bool Window::KeyIsDown(Key key) const {
    return handle_ && glfwGetKey(handle_, ToGlfw(key)) == GLFW_PRESS;
}

bool Window::MouseIsDown(MouseButton button) const {
    return handle_ && glfwGetMouseButton(handle_, ToGlfw(button)) == GLFW_PRESS;
}

Vec2d Window::CursorPos() const {
    if (!handle_) return {};
    double x = 0.0, y = 0.0;
    glfwGetCursorPos(handle_, &x, &y);
    return {x, y};
}

void Window::SetCursorPos(Vec2d pos) const {
    if (handle_) glfwSetCursorPos(handle_, pos.x, pos.y);
}

void Window::SetCursorVisible(bool visible) const {
    if (handle_)
        glfwSetInputMode(handle_, GLFW_CURSOR,
                         visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
}

void Window::SetCursorCaptured(bool captured) const {
    // DISABLED hides the cursor and confines it to the window: GLFW keeps
    // reporting absolute positions, so demos re-anchor lastX/lastY themselves
    // (the voxel fly camera does). No GLFW constant leaks into demo code.
    if (handle_)
        glfwSetInputMode(handle_, GLFW_CURSOR,
                         captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void Window::SetRawMouseInput(bool enabled) const {
    // GLFW refuses raw motion unless the platform supports it; the call is a
    // no-op there, which is an acceptable degradation for a demo.
    if (handle_) glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION,
                                  enabled ? GLFW_TRUE : GLFW_FALSE);
}

// ---- window queries and actions ----------------------------------------------

Vec2d Window::Pos() const {
    if (!handle_) return {};
    int x = 0, y = 0;
    glfwGetWindowPos(handle_, &x, &y);
    return {static_cast<double>(x), static_cast<double>(y)};
}

void Window::SetPos(Vec2d topLeft) const {
    if (handle_) glfwSetWindowPos(handle_, static_cast<int>(topLeft.x),
                                  static_cast<int>(topLeft.y));
}

Vec2d Window::Size() const {
    if (!handle_) return {};
    int w = 0, h = 0;
    glfwGetWindowSize(handle_, &w, &h);
    return {static_cast<double>(w), static_cast<double>(h)};
}

Vec2d Window::FramebufferSize() const {
    if (!handle_) return {};
    int w = 0, h = 0;
    glfwGetFramebufferSize(handle_, &w, &h);
    return {static_cast<double>(w), static_cast<double>(h)};
}

void Window::Focus() const {
    if (handle_) glfwFocusWindow(handle_);
}

void Window::SetTitle(const std::string& title) {
    if (handle_) glfwSetWindowTitle(handle_, title.c_str());
}

bool Window::ContextIsCurrent() const {
    return handle_ && glfwGetCurrentContext() == handle_;
}

bool Window::CaptureScreenshot(const std::string& path) const {
    if (!handle_) return false;
    glfwMakeContextCurrent(handle_);

    // Read whatever the last render pass left in the viewport: every pass sets
    // GL_VIEWPORT to the full default framebuffer, so this is the whole window
    // at its real (content-scaled) size, not the requested logical size.
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    const int width = vp[2];
    const int height = vp[3];
    if (width <= 0 || height <= 0) return false;

    // Tight row packing (default 4-byte alignment would pad odd widths), and read
    // the back buffer that was just drawn, before the swap makes it the front.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);

    const std::size_t count = static_cast<std::size_t>(width) * height * 3;
    std::vector<std::uint8_t> rows(count);
    glGetError();  // clear any latched error so the check below is meaningful
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rows.data());
    if (glGetError() != GL_NO_ERROR) return false;

    // Flip to top-down here so the engine hook receives one convention only.
    std::vector<std::uint8_t> flipped(count);
    const std::size_t stride = static_cast<std::size_t>(width) * 3;
    for (int y = 0; y < height; ++y)
        std::memcpy(&flipped[static_cast<std::size_t>(y) * stride],
                    &rows[static_cast<std::size_t>(height - 1 - y) * stride], stride);

    return gldx::EncodeScreenshot(path, width, height, flipped.data());
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
