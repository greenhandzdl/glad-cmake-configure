// ============================================================================
// gldxwin - C++20 named module (primary interface unit)
//
// An engine-agnostic GLFW windowing + frame-loop library. It exists so the
// per-executable boilerplate that used to live in src/demo/demo_app.h
// (glfwInit -> GL 4.1 core hints -> create window -> makeContextCurrent ->
// gladLoadGL -> frame loop -> ordered teardown) is written once, correctly, and
// reused. Unlike demo_app.h it does NOT touch the engine: gldxwin has no
// gldx::Renderer, imports no gldx type, and links only glfw + GLAD. A demo
// reaches the engine by importing gldx itself and owns its renderer through the
// Window's user-data slot.
//
// Two types carry the whole contract:
//   * App    - a process-wide Meyers singleton owning glfwInit/glfwTerminate and
//              the GL context hints, plus the multi-window frame loop (Run).
//   * Window - one OS window. Constructing it registers with App; it creates the
//              GLFW window, makes its context current and loads GLAD. Callbacks
//              (OnCreate / OnFrame / OnDestroy) let a demo build, drive and tear
//              down its GL resources strictly while a context is current.
//
// Window also owns the *input surface*: key/char/mouse-button/cursor/scroll
// callbacks and polled state (KeyIsDown & co) are expressed with the portable
// enums below, so a demo never needs to include GLFW or name a GLFW constant.
// gldxwin therefore installs those GLFW callbacks itself and reserves the
// window's GLFW user-pointer for the trampoline - demos must not call
// glfwSetWindowUserPointer; use the SetUserData slot or capture the Window in
// the callback lambda instead. Handle() remains only as the escape hatch for
// window-system capabilities this layer deliberately does not proxy (raw mouse
// move, clipboard, joystick, file drop): "rare and GLFW-typed - do not proxy;
// frequent and type-clean - proxy" is the binding rule.
//
// The contract every consumer must respect (the point of the whole library):
//   * GL resources are created in OnCreate and destroyed in OnDestroy - both run
//     on the render thread with the window's context current, and OnDestroy runs
//     before glfwDestroyWindow, so destructors that issue GL calls are safe.
//   * gldx::RenderContext::MarkAsRenderThread() is called by the demo at the top
//     of OnCreate (not by gldxwin, which must stay engine-agnostic).
//   * There is a single render thread: Run() drives every window in order on the
//     thread that owns the windows, satisfying the engine's single-threaded-GL
//     rule without the library knowing anything about the engine.
//
// Global module fragment: glad + glfw + the small std set attach to the global
// module (see gmf.hpp), so GLFWwindow* and std::function in the exported
// signatures are global-module types - the same trick the engine uses for
// glad/glm, and why a demo text-includes gldx/core/Platform.h alongside
// `import gldxwin;` to call glfwGetKey & co. itself.
// ============================================================================

module;

#include "gldxwin/gmf.hpp"

export module gldxwin;

export namespace gldx::win {

// Portable input vocabulary. The values carry no GLFW meaning; Window.cpp owns
// the one mapping table, so a demo switches on these names instead of GLFW_*
// constants and compiles without <GLFW/glfw3.h> in scope.
enum class KeyAction { Press, Release, Repeat };

enum class Key {
    Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Escape, Tab, Space, Enter, Backspace,
    Left, Right, Up, Down,
    LeftBracket, RightBracket, Minus, Equal,
    LeftControl, LeftShift,
};

enum class MouseButton { Left, Right, Middle };

// A 2D coordinate pair for cursor / window / framebuffer queries. Deliberately
// not glm::vec2: gldxwin must stay engine-agnostic (no gldx / GLM dependency).
struct Vec2d {
    double x = 0.0;
    double y = 0.0;
};

// How a Window should come up. Plain defaults so a demo can pass `WindowDesc{}`
// or set just the fields it cares about. `title` is a borrowed C string (GLFW's
// own convention); it is only read during construction.
struct WindowDesc {
    int         width     = 800;
    int         height    = 600;
    const char* title     = "gldx";
    bool        resizable = true;
};

// Knobs for App::Run. quitAfterSeconds mirrors the gldxcli `--quit-after`
// switch: > 0 closes every window that many seconds into the loop so a headless
// regression sweep terminates on its own. gldxwin does not depend on gldxcli,
// so the demo reads the flag and hands the number here.
struct RunOptions {
    double quitAfterSeconds = 0.0;
};

class Window;

// Everything a per-frame callback needs, gathered by Run so demos do not each
// re-derive framebuffer size, elapsed time or FPS. `window` is the gldxwin
// Window (reach the native handle via window->Handle()); no engine type appears.
struct FrameInfo {
    Window* window      = nullptr;
    int     fbWidth     = 0;
    int     fbHeight    = 0;
    double  time        = 0.0;   // seconds since Run() started the loop
    double  dt          = 0.0;   // seconds since this window's previous frame
    double  smoothedFps = 60.0;  // averaged over a fixed 0.5s wall-clock window
};

// One OS window and its per-frame callbacks. Registering with App happens in the
// constructor and unregistration in the destructor, so a demo simply declares
// Window objects (stack or heap) and App::Run drives whichever are alive.
class Window {
public:
    explicit Window(const WindowDesc& desc = {});
    ~Window();

    Window(const Window&)            = delete;
    Window& operator=(const Window&) = delete;

    // True once the GLFW window and a current GL context exist. A false window
    // is inert: Run() skips it, so a failed creation degrades to "nothing to
    // draw" rather than a crash.
    [[nodiscard]] bool Ok() const noexcept { return handle_ != nullptr; }

    // The native handle, an explicit escape hatch for window-system capabilities
    // this library does not proxy (raw mouse move, clipboard, joystick, file
    // drop). Everything a normal demo needs - input, geometry, context checks,
    // screenshots - goes through the methods below, so keeping Handle() unused
    // is the design goal. Valid only when Ok().
    [[nodiscard]] GLFWwindow* Handle() const noexcept { return handle_; }

    [[nodiscard]] const WindowDesc& Desc() const noexcept { return desc_; }
    [[nodiscard]] int Width() const noexcept { return desc_.width; }
    [[nodiscard]] int Height() const noexcept { return desc_.height; }

    // Esc closes the window by default; pass false for demos that use Esc for
    // something else (pointer lock, menu) and end purely via --quit-after.
    void SetCloseOnEsc(bool enabled) noexcept { closeOnEsc_ = enabled; }

    [[nodiscard]] bool ShouldClose() const;
    void Close() const;

    // ---- Input: event callbacks ------------------------------------------------
    // All fire on the render thread inside Run()'s glfwPollEvents. The latest
    // setter wins per event kind; an empty std::function unsubscribes. gldxwin
    // registers the GLFW trampolines (and clears them before destroying the
    // window, which GLFW requires for mouse callbacks); demos never call
    // glfwSet*Callback on this handle.
    void OnKey(std::function<void(Window&, Key, KeyAction, int mods)> cb);
    void OnChar(std::function<void(Window&, unsigned int codepoint)> cb);
    void OnMouseButton(std::function<void(Window&, MouseButton, KeyAction, int mods)> cb);
    // Cursor positions are window (logical) coordinates.
    void OnCursor(std::function<void(Window&, Vec2d)> cb);
    void OnScroll(std::function<void(Window&, double dx, double dy)> cb);

    // ---- Input: polled state ---------------------------------------------------
    // Read the *current context's* window (GLFW's own rule). Safe from OnFrame
    // (Run makes each window current before its frame) and from single-window
    // demos; in a multi-window loop, only the window whose context is current
    // answers correctly - register a callback instead when in doubt.
    [[nodiscard]] bool KeyIsDown(Key key) const;
    [[nodiscard]] bool MouseIsDown(MouseButton button) const;
    [[nodiscard]] Vec2d CursorPos() const;
    void SetCursorPos(Vec2d pos) const;
    void SetCursorVisible(bool visible) const;      // false = hide the cursor
    void SetCursorCaptured(bool captured) const;    // true = disable + lock to centre
    void SetRawMouseInput(bool enabled) const;      // GLFW raw cursor-motion mode

    // ---- Window queries and actions (current-context rules as above) -----------
    [[nodiscard]] Vec2d Pos() const;                // top-left on screen
    void SetPos(Vec2d topLeft) const;
    [[nodiscard]] Vec2d Size() const;               // logical (content) size
    [[nodiscard]] Vec2d FramebufferSize() const;    // pixel size (Retina ~2x)
    void Focus() const;
    void SetTitle(const std::string& title);

    // Per-window screenshot plumbing: make this window current, read the region
    // the last pass left in GL_VIEWPORT, hand the tight-packed bottom-up RGB row
    // buffer to gldx::EncodeScreenshot (PNG lives in the engine, gldxwin stays
    // engine-agnostic and only forward-declares the hook).
    bool CaptureScreenshot(const std::string& path) const;

    // True when this window's context is the calling thread's current context -
    // the multi-window loop's per-frame sanity check, without the demo having to
    // call glfwGetCurrentContext/Handle() itself.
    [[nodiscard]] bool ContextIsCurrent() const;

    // Lifecycle hooks. OnCreate fires once before the first frame (build GL
    // resources, call MarkAsRenderThread); OnFrame fires every frame; OnDestroy
    // fires once before the GLFW window is destroyed with the context still
    // current (release GL resources). All run on the render thread.
    void OnCreate(std::function<void(Window&)> cb)   { onCreate_  = std::move(cb); }
    void OnFrame(std::function<void(FrameInfo&)> cb) { onFrame_   = std::move(cb); }
    void OnDestroy(std::function<void(Window&)> cb)  { onDestroy_ = std::move(cb); }

    // A single slot for the demo to hang its renderer/resources on. Not owned.
    void  SetUserData(void* data) noexcept { userData_ = data; }
    void* GetUserData() const noexcept { return userData_; }

    template <class T>
    [[nodiscard]] T* UserDataAs() const noexcept { return static_cast<T*>(userData_); }

private:
    // GLFW trampolines, defined in Window.cpp (internal linkage, module-only
    // visibility). They are declared here so the definition can be a friend and
    // dispatch through the private on*_ callbacks; Run()/the constructor install
    // and clear them. No consumer outside gldxwin can even name them.
    static void TrampKey(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void TrampChar(GLFWwindow* w, unsigned int codepoint);
    static void TrampMouseButton(GLFWwindow* w, int button, int action, int mods);
    static void TrampCursor(GLFWwindow* w, double x, double y);
    static void TrampScroll(GLFWwindow* w, double dx, double dy);

    // Run() drives these; kept private so timing state stays consistent.
    void ResetTiming(double startedAt);
    void FillFrameInfo(FrameInfo& out, double startedAt, double now);
    void RunCreate();
    void RunDestroy();

    WindowDesc desc_;
    GLFWwindow* handle_ = nullptr;

    std::function<void(Window&)>   onCreate_;
    std::function<void(FrameInfo&)> onFrame_;
    std::function<void(Window&)>   onDestroy_;

    // Input callbacks, driven by the GLFW trampolines installed in Window.cpp
    // (the GLFW window user-pointer is reserved to point back at `this`).
    std::function<void(Window&, Key, KeyAction, int)>      onKey_;
    std::function<void(Window&, unsigned int)>             onChar_;
    std::function<void(Window&, MouseButton, KeyAction, int)> onButton_;
    std::function<void(Window&, Vec2d)>                    onCursor_;
    std::function<void(Window&, double, double)>           onScroll_;

    void* userData_    = nullptr;
    bool  closeOnEsc_  = true;
    bool  createdHook_ = false;   // guards OnCreate firing exactly once

    // Per-window frame pacing + FPS average (mirrors the old Ctx::Loop locals).
    double prevTime_       = 0.0;
    double fpsWindowStart_ = 0.0;
    int    fpsFrames_      = 0;
    double smoothedFps_    = 60.0;

    friend class App;
};

// Process-wide GLFW owner and frame-loop driver. A Meyers singleton: the first
// Get() runs glfwInit and sets the GL 4.1 core hints (forward-compat on macOS);
// the static destructor runs glfwTerminate after every Window (constructed by
// the demo in main) has already been destroyed, so GL teardown is never racing
// a dead context. Constructing a Window pulls the singleton in, so ordering is
// automatic.
class App {
public:
    static App& Get();

    App(const App&)            = delete;
    App& operator=(const App&) = delete;

    // False if glfwInit failed; Window construction then yields inert windows.
    [[nodiscard]] bool Ok() const noexcept { return ok_; }

    // Drives every live, ok window until all have closed (Esc, the OS, or
    // quitAfterSeconds). Returns 0 normally, non-zero if glfwInit never
    // succeeded. Single-threaded by construction.
    int Run(const RunOptions& options = {});

    [[nodiscard]] bool AnyWindowOpen() const;

    // GLFW's global timer (seconds since glfwInit). Exposed so demos stop
    // calling glfwGetTime directly; per-frame time is already in FrameInfo.
    [[nodiscard]] static double Now();

private:
    App();
    ~App();

    void Register(Window* window);
    void Unregister(Window* window);

    bool ok_ = false;
    std::vector<Window*> windows_;

    friend class Window;
};

} // namespace gldx::win
