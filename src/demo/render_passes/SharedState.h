#ifndef RENDER_PASSES_SHARED_STATE_H
#define RENDER_PASSES_SHARED_STATE_H

/**
 * @file SharedState.h
 * @brief The one CPU-side truth every window renders, plus the background
 *        worker that advances it. Pure std on purpose — no gldx / gldxwin
 *        import — so the worker side of this demo can never reach GL by
 *        accident, which is the point of the multi-context split.
 *
 * Every window here owns its own GL context and therefore its own private
 * programs / VAOs / VBOs. The only thing they share is this struct: a handful
 * of atomics the worker thread keeps advancing and that each window's render
 * pass samples once per frame. Because all windows read the same atomic clock,
 * the shapes animate in lockstep across every context — the visible proof that
 * the windows are synchronised on CPU state while staying GPU-isolated.
 */

#include <atomic>
#include <thread>

namespace render_passes {

constexpr int kMaxWindows = 6;

template <class T>
constexpr T Clamp(T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

// The shared truth. Written by the worker (phase / ticks) and read by every
// window's passes on the render thread; freeze is set by the screenshot burst
// so all windows capture the identical animation frame.
struct SharedState {
    std::atomic<double>   phase {0.0};   // animation clock in seconds (worker-driven)
    std::atomic<unsigned> ticks {0};     // worker heartbeat, printed per burst
    std::atomic<bool>     freeze{false}; // stop the clock for a pixel-diffable shot
};

// Background producer: advances the clock and counts heartbeats. Deliberately
// incapable of touching GL — it only publishes numbers the render thread reads.
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

} // namespace render_passes

#endif // RENDER_PASSES_SHARED_STATE_H
