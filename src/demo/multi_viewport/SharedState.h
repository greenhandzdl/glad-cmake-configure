#ifndef MULTI_VIEWPORT_SHARED_STATE_H
#define MULTI_VIEWPORT_SHARED_STATE_H

/**
 * @file SharedState.h
 * @brief The one source of truth every view renders, plus the background
 *        worker thread that advances it. CPU-side only — no GL handle ever
 *        crosses this line, which is the whole point of the demo.
 *
 * SharedState is written by the render thread (drag callbacks) and by the
 * worker, and read by both — hence atomics, hence a fair TSan subject.
 * Worker is the producer: it advances the animation clock and adds a slow
 * auto-orbit, and is deliberately incapable of touching GL.
 *
 * This header is pure std on purpose - no gldx / gldxwin import - so the
 * worker side of the demo can never reach GL by accident.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace multi_viewport {

constexpr float  kPi        = 3.14159265358979323846f;
constexpr int    kMaxWindows = 6;
constexpr double kSpinSpeed = 1.6;    // rad of cube spin per elapsed second

template <class T>
constexpr T Clamp(T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// The single source of truth every view renders. Written by the input
// callbacks and the background worker thread, read by the render loop.
struct SharedState {
    std::atomic<float>    orbitYaw  {0.9f};
    std::atomic<float>    orbitPitch{0.42f};
    std::atomic<double>   phase {0.0};   // animation clock, seconds * kSpinSpeed
    std::atomic<unsigned> ticks{0};      // worker heartbeat shown in the HUD
    std::atomic<bool>     freeze{false}; // screenshot burst: stop the clock so
                                         // every window captures the SAME phase
};

// Background producer: advances the clock and adds a slow auto-orbit.
// Deliberately incapable of touching GL - it only publishes numbers the render
// thread samples.
class Worker {
public:
    explicit Worker(SharedState& state);
    ~Worker();

    Worker(const Worker&)            = delete;
    Worker& operator=(const Worker&) = delete;

private:
    SharedState&      state_;
    std::thread       thread_;
    std::atomic<bool> stop_{false};
};

} // namespace multi_viewport

#endif // MULTI_VIEWPORT_SHARED_STATE_H
