// multi_window_levels demo — shared sync clock + worker thread (see SharedState.h).
#include "SharedState.h"

namespace multi_window_levels {

Worker::Worker(SharedState& state) : state_(state) {
    thread_ = std::thread([this] {
        const auto started = std::chrono::steady_clock::now();
        while (!stop_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (state_.freeze.load(std::memory_order_relaxed)) continue;
            const double t = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            state_.syncPhase.store(t * kSpinSpeed, std::memory_order_relaxed);
            state_.ticks.fetch_add(1, std::memory_order_relaxed);
        }
    });
}

Worker::~Worker() { stop_.store(true); if (thread_.joinable()) thread_.join(); }

} // namespace multi_window_levels
