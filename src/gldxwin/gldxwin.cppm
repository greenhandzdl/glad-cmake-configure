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

    // The native handle, for the demo's own glfwGetKey / glfwSet*Callback /
    // glfwSetWindowUserPointer input plumbing. Valid only when Ok().
    [[nodiscard]] GLFWwindow* Handle() const noexcept { return handle_; }

    [[nodiscard]] const WindowDesc& Desc() const noexcept { return desc_; }
    [[nodiscard]] int Width() const noexcept { return desc_.width; }
    [[nodiscard]] int Height() const noexcept { return desc_.height; }

    // Esc closes the window by default; pass false for demos that use Esc for
    // something else (pointer lock, menu) and end purely via --quit-after.
    void SetCloseOnEsc(bool enabled) noexcept { closeOnEsc_ = enabled; }

    [[nodiscard]] bool ShouldClose() const;
    void Close() const;

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
