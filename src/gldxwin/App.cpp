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

#include <cstdint>
#include <cstdlib>

module gldxwin;

namespace gldx::win {

namespace {

// Verification harness (opt-in, env-gated): read a window's default framebuffer
// back and drop it as a 24-bit BMP so a headless sweep can prove a scene actually
// produced pixels rather than only that main() returned 0 (a blank window and a
// fully-rendered one both exit 0). BMP is emitted with plain std + GL readback,
// so it adds no image-library dependency to this engine-agnostic library. Lives
// entirely behind the GLDX_SNAPSHOT environment variable; unset, it is dead code.
bool WriteFramebufferBmp(GLFWwindow* handle, const char* path) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(handle, &width, &height);
    if (width <= 0 || height <= 0) return false;

    const int rowBytes = width * 3;
    const int paddedRow = (rowBytes + 3) & ~3;   // BMP rows align to 4 bytes
    const std::size_t pixelBytes = static_cast<std::size_t>(paddedRow) * height;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(rowBytes) * height);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glGetError();
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    if (glGetError() != GL_NO_ERROR) return false;

    std::vector<std::uint8_t> bgr(pixelBytes, 0);
    for (int y = 0; y < height; ++y) {
        // Positive-height BMP is bottom-up, exactly like the GL readback, so each
        // row copies straight through; only the channel order flips.
        const std::uint8_t* src = &rgb[static_cast<std::size_t>(y) * rowBytes];
        std::uint8_t* dst = &bgr[static_cast<std::size_t>(y) * paddedRow];
        for (int x = 0; x < rowBytes; x += 3) {
            dst[x + 0] = src[x + 2];
            dst[x + 1] = src[x + 1];
            dst[x + 2] = src[x + 0];
        }
    }

    std::uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M';
    auto put32 = [&](int off, std::uint32_t v) {
        header[off] = static_cast<std::uint8_t>(v & 0xFF);
        header[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        header[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        header[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    };
    put32(2, 54 + static_cast<std::uint32_t>(pixelBytes));   // file size
    put32(10, 54);                                            // pixel data offset
    put32(14, 40);                                            // info header size
    put32(18, static_cast<std::uint32_t>(width));
    put32(22, static_cast<std::uint32_t>(height));
    header[26] = 1;                                           // planes
    header[28] = 24;                                          // bits per pixel
    put32(34, static_cast<std::uint32_t>(pixelBytes));

    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite(header, 1, sizeof(header), f);
    std::fwrite(bgr.data(), 1, pixelBytes, f);
    std::fclose(f);
    return true;
}

} // namespace

App& App::Get() {
    static App instance;
    return instance;
}

App::App() {
    // glfwInit first: it resets the window hints to defaults, so the GL 4.1 core
    // hints below would be clobbered if set beforehand (the ordering the old
    // demo_app.h relied on). Hints then apply to every window created after.
    if (!glfwInit()) {
        std::fprintf(stderr, "gldxwin: failed to initialize GLFW\n");
        ok_ = false;
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    // Request a depth + stencil buffer explicitly. GLFW's documented defaults
    // are already 24/8, but the direct-to-window render path (BeginSceneTarget
    // binds FB0 for demos that bring no PostProcessChain) depth-tests against
    // this buffer, so asking by name keeps that contract independent of any
    // platform default. (Note: Apple's GLFW reports GLFW_DEPTH_BITS==0 from
    // glfwGetWindowAttrib even though the window's default framebuffer does
    // carry a working depth buffer — verified empirically, so do not treat a 0
    // readback here as "no depth".)
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
#if defined(GLFW_PLATFORM_MACOS) && GLFW_PLATFORM_MACOS
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    // GLFW_PLATFORM_MACOS comes from gldx/core/Platform.h, which importers
    // textually include; when gldxwin is compiled standalone it is absent, so
    // fall back to the Apple detector GLFW itself uses.
#if !defined(GLFW_PLATFORM_MACOS) && defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

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

    // Snapshot harness knobs (see WriteFramebufferBmp). Off unless GLDX_SNAPSHOT
    // names a file; GLDX_SNAPSHOT_AT picks the capture time (default 2s in, once
    // the pipeline has warmed and any --freeze-at has settled the animation).
    const char* const snapPath = std::getenv("GLDX_SNAPSHOT");
    const bool snapEnabled = snapPath && *snapPath;
    const char* const snapAtEnv = std::getenv("GLDX_SNAPSHOT_AT");
    const double snapAt = (snapAtEnv && *snapAtEnv) ? std::atof(snapAtEnv) : 2.0;
    bool snapDone = false;

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

            // Capture after the demo has drawn this frame but before the swap, so
            // the back buffer still holds the fresh image. One window is enough to
            // prove the scene rendered; then end the run.
            if (snapEnabled && !snapDone && (now - startedAt) >= snapAt) {
                snapDone = true;
                glfwMakeContextCurrent(w->handle_);
                if (WriteFramebufferBmp(w->handle_, snapPath))
                    std::printf("gldxwin: snapshot %s\n", snapPath);
                else
                    std::fprintf(stderr, "gldxwin: snapshot failed\n");
                w->Close();
            }

            glfwSwapBuffers(w->handle_);
        }
    }
    return 0;
}

} // namespace gldx::win
