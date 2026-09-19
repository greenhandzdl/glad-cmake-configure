#ifndef GFX_DEBUG_PROFILER_H
#define GFX_DEBUG_PROFILER_H

/**
 * @file Profiler.h
 * @brief Per-frame CPU + GPU timing (plan "Profiler", GL_ARB_timer_query which
 *        is core in OpenGL 3.3+ / available under the 4.1 baseline).
 *
 * Usage, wrapped around the frame's GL work (render thread):
 *   prof.BeginFrame();   // starts a GL_TIME_ELAPSED query + snapshots CPU clock
 *   ...draw...           // also calls glEndFrame timing
 *   prof.EndFrame();     // closes the query, flips the double buffer, updates ms
 *
 * GPU elapsed time is only known one frame later (async query), which is normal
 * for timer queries; the read-back of the previous frame's query happens inside
 * BeginFrame so there is never a synchronous stall.
 */

#include <chrono>

#include <glad/gl.h>

namespace gfx {

class Profiler {
public:
    Profiler() = default;
    ~Profiler();

    Profiler(const Profiler&)            = delete;
    Profiler& operator=(const Profiler&) = delete;

    void Init();          // glGenQueries (render thread)

    void BeginFrame();
    void EndFrame();

    [[nodiscard]] float CpuMs() const noexcept { return cpuMs_; }
    [[nodiscard]] float GpuMs() const noexcept { return gpuMs_; }

private:
    static constexpr float kSmoothing = 0.1f;

    GLuint queries_[2] = {0, 0};
    int cur_ = 0;
    bool ready_ = false;

    std::chrono::steady_clock::time_point cpuStart_{};
    float cpuMs_ = 0.0f;
    float gpuMs_ = 0.0f;
};

} // namespace gfx

#endif // GFX_DEBUG_PROFILER_H
