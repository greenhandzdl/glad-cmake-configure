#ifndef MULTI_WINDOW_LEVELS_SHARED_STATE_H
#define MULTI_WINDOW_LEVELS_SHARED_STATE_H

/**
 * @file SharedState.h
 * @brief The single synchronised clock, and the background worker that advances
 *        it. CPU-side only - no GL handle ever crosses this line.
 *
 * Only the Sync windows read syncPhase; the Async windows deliberately do not
 * touch anything here (each accumulates its own private phase in View). That
 * asymmetry IS the demo's point: this struct is the whole extent of what the
 * synchronised windows share, and the async/island windows prove you can opt
 * out of it entirely and stay correct.
 *
 * Written by the worker thread, read by the render thread - hence atomics.
 * The header is pure std (no gldx / gldxwin import) so the worker side can
 * never reach GL by accident.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "Profiles.h"

namespace multi_window_levels {

// The shared truth the Sync windows sample. Async windows keep their phase
// private and never read this.
struct SharedState {
    std::atomic<double>   syncPhase{0.0};  // seconds * kSpinSpeed, worker-driven
    std::atomic<unsigned> ticks{0};        // worker heartbeat shown in the HUD
    std::atomic<bool>     freeze{false};   // screenshot burst: stop the clock
};

// Background producer: advances the shared clock and the tick counter, nothing
// else. Deliberately incapable of touching GL - it only publishes numbers the
// render thread samples.
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

} // namespace multi_window_levels

#endif // MULTI_WINDOW_LEVELS_SHARED_STATE_H
