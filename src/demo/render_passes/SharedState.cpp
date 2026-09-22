// render_passes demo — the background producer (see SharedState.h).
#include "SharedState.h"

#include <chrono>

namespace render_passes {

Worker::Worker(SharedState& state) : state_(state) {
    thread_ = std::thread([this] {
        // Publish the wall-clock seconds since the worker started as the shared
        // animation phase, and bump a heartbeat. No GL here, ever; this thread
        // only writes atomics the render thread samples. A screenshot burst sets
        // freeze and the loop stops advancing, so every window captures the same
        // phase. (phase is derived from the clock rather than fetch_add'd, to
        // match the multi_viewport worker and avoid float-atomic subtleties.)
        const auto started = std::chrono::steady_clock::now();
        while (!stop_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            if (state_.freeze.load(std::memory_order_relaxed)) continue;
            const double t = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            state_.phase.store(t, std::memory_order_relaxed);
            state_.ticks.fetch_add(1, std::memory_order_relaxed);
        }
    });
}

Worker::~Worker() {
    stop_.store(true, std::memory_order_relaxed);
    if (thread_.joinable()) thread_.join();
}

} // namespace render_passes
