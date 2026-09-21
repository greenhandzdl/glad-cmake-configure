#ifndef DEMO_APP_H
#define DEMO_APP_H

/**
 * @file demo_app.h
 * @brief Shared application-side scaffolding for the src/demo/{feature} demos.
 *
 * Deliberately *not* part of module gldx (same rationale as demo_cli.h): this is
 * executable plumbing, not an engine type. Every demo used to repeat the same
 * ~40 lines of `glfwInit -> context hints -> createWindow -> makeContextCurrent
 * -> gladLoad -> MarkAsRenderThread -> frame loop -> lambda-scoped teardown`,
 * and the teardown ordering is easy to get subtly wrong. Run() owns that
 * lifecycle once so a demo's main() only has to build its own passes/resources
 * and fill a RenderFrame per frame.
 *
 * The one contract callers must respect: every GPU-resource owner is constructed
 * *inside* the `app` callback and destroyed before it returns. Run() calls
 * glfwTerminate() only after app() comes back, so destructors that issue GL
 * (ShaderProgram, Mesh, Framebuffer, ...) still run on the render thread with a
 * live context. The original hand-rolled demos enforced this with a lambda; the
 * `app` callback IS that lambda, just supplied by Run's caller.
 *
 * All GL still happens on a single render thread; nothing here uploads off it.
 */

// Plain text includes (not module members): Platform.h orders <glad/gl.h> before
// <GLFW/glfw3.h> and carries the window/title constants + GLFW_PLATFORM_* macros.
#include "gldx/core/Platform.h"

#include <functional>
#include <iostream>

#include "demo/demo_cli.h"

// The whole engine as a single C++20 named module. Importing here means each
// demo TU gets gldx simply by including this header; a repeated `import gldx;`
// in the demo itself would be a harmless no-op, so none is required.
import gldx;

namespace demo {

// Everything a per-frame draw callback tends to need, gathered by Run's loop so
// demos do not each re-derive the framebuffer size, elapsed time or FPS.
struct FrameInfo {
    GLFWwindow* window = nullptr;
    int      fbWidth   = 0;
    int      fbHeight  = 0;
    double   time      = 0.0;   // seconds since the frame loop started
    double   dt        = 0.0;   // seconds since the previous frame
    double   smoothedFps = 60.0;
};

// Handed to the app callback. Owns nothing; `renderer` and `window` are valid
// only for the duration of the Run() call that created this Ctx.
class Ctx {
public:
    Ctx(GLFWwindow* w, gldx::Renderer& r, const Flags& f) noexcept
        : window(w), renderer(r), flags(f) {}

    Ctx(const Ctx&)            = delete;
    Ctx& operator=(const Ctx&) = delete;

    // Drive frames until the window closes (Esc, the OS, or --quit-after). Each
    // iteration builds an empty RenderFrame (with fb size + fps pre-filled),
    // lets `draw` attach whatever subsystems the demo is showing, then runs the
    // renderer. Returns 0; the app callback propagates any real exit code.
    int Loop(const std::function<void(gldx::RenderFrame&, const FrameInfo&)>& draw);

    GLFWwindow*  window;
    gldx::Renderer& renderer;
    const Flags& flags;
};

// Brings up a GL 4.1 core context and runs `app` on the render thread. `title`
// is the window caption; a failure path returns non-zero without ever calling
// app(). On success app()'s return value becomes Run()'s.
inline int Run(const Flags& flags, const char* title, const std::function<int(Ctx&)>& app) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if GLFW_PLATFORM_MACOS
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(gldx::kWindowWidth, gldx::kWindowHeight, title, nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window (OpenGL 4.1 core?)\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    gldx::RenderContext::MarkAsRenderThread();

    std::printf("%s %s\n", gldx::kAppName, gldx::kAppVersion);
    std::printf("OpenGL %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    gldx::Renderer renderer;
    renderer.Init();   // global depth/cull state; demos then add or build passes

    Ctx ctx(window, renderer, flags);
    const int rc = app(ctx);   // demo resources live here and destruct on return

    glfwDestroyWindow(window);
    glfwTerminate();
    return rc;
}

inline int Ctx::Loop(const std::function<void(gldx::RenderFrame&, const FrameInfo&)>& draw) {
    const double startedAt = glfwGetTime();
    const double quitAfter = flags.quitAfter();
    double smoothedFps = 60.0;
    int    fpsFrames = 0;
    double fpsWindowStart = startedAt;
    double prev = startedAt;

    while (!glfwWindowShouldClose(window)) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        const double now = glfwGetTime();
        const double time = now - startedAt;
        const double dt = now - prev;
        prev = now;

        // FPS averaged over a fixed wall-clock window rather than an EMA of 1/dt,
        // so the HUD number matches the frame it is drawn on.
        ++fpsFrames;
        if (const double span = now - fpsWindowStart; span >= 0.5) {
            smoothedFps = static_cast<double>(fpsFrames) / span;
            fpsFrames = 0;
            fpsWindowStart = now;
        }
        if (quitAfter > 0.0 && time >= quitAfter)
            glfwSetWindowShouldClose(window, true);

        int fbw = 0, fbh = 0;
        glfwGetFramebufferSize(window, &fbw, &fbh);

        gldx::RenderFrame frame;
        frame.fbWidth = fbw;
        frame.fbHeight = fbh;
        frame.smoothedFps = smoothedFps;

        const FrameInfo info{window, fbw, fbh, time, dt, smoothedFps};
        draw(frame, info);
        renderer.Render(frame);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    return 0;
}

} // namespace demo

#endif // DEMO_APP_H
