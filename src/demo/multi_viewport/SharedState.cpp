// multi_viewport demo — shared CPU state + worker thread (see SharedState.h).
#include "SharedState.h"

namespace multi_viewport {

Worker::Worker(SharedState& state) : state_(state) {
    thread_ = std::thread([this] {
        const auto started = std::chrono::steady_clock::now();
        while (!stop_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (state_.freeze.load(std::memory_order_relaxed)) continue;
            const double t = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            state_.phase.store(t * kSpinSpeed, std::memory_order_relaxed);
            state_.ticks.fetch_add(1, std::memory_order_relaxed);
            float yaw = state_.orbitYaw.load(std::memory_order_relaxed) + 0.02f;
            if (yaw > 2.0f * kPi) yaw -= 2.0f * kPi;
            state_.orbitYaw.store(yaw, std::memory_order_relaxed);
        }
    });
}

Worker::~Worker() { stop_.store(true); if (thread_.joinable()) thread_.join(); }

} // namespace multi_viewport
