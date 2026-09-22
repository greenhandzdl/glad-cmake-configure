// multi_window_levels demo — the graded window profiles (see Profiles.h).
#include "Profiles.h"

namespace multi_window_levels {

// Five rows, each nudging one or more of the sync/async, viewpoint, reach, and
// detail axes so the windows spread across the multi-window design space. The
// two Sync rows share the atomic clock (matching phase, divergent culling from
// opposite eye points); the two Async rows each run their own clock; the last
// row is the fully-unrelated island context.
const std::vector<Profile>& ProfileTable() {
    static const std::vector<Profile> table = {
        // flagship: highest tier, sync clock, wide reach, smooth spheres.
        { "flagship",  Clock::Sync,  1.0,  0.0,
          0.9f, 0.40f, 11.0f, 55.0f, 300.0f, 32, false, {0.90f, 0.50f, 0.25f} },
        // rear-guard: same field, SAME sync clock, but the eye is swung 180
        // degrees around - so its frustum keeps the objects flagship dropped
        // and vice versa. Phase matches flagship; visible set does not.
        { "rear-guard",Clock::Sync,  1.0,  0.0,
          0.9f + kPi, 0.30f, 11.0f, 55.0f, 300.0f, 24, false, {0.30f, 0.72f, 0.90f} },
        // scout: async, slow-motion (rate 0.35), tight fov + short far plane and
        // chunky low-poly spheres - the most aggressive culling and lowest tier.
        { "scout",     Clock::Async, 0.35, 1.1,
          2.4f, 0.18f, 7.0f, 28.0f, 12.0f, 8, false, {0.45f, 0.85f, 0.40f} },
        // drifter: async on its OWN clock at normal rate, medium reach. Compare
        // its phase to scout's (different rate) and to the sync pair's (shared
        // atomic) - all three disagree, which is the async proof.
        { "drifter",   Clock::Async, 1.0,  0.0,
          -1.3f, 0.52f, 14.0f, 60.0f, 60.0f, 20, false, {0.85f, 0.40f, 0.75f} },
        // island: independent context. Shares nothing with the field windows -
        // not the scene, not the pipeline, not the clock. Draws its own lines.
        { "island",    Clock::Async, 1.0,  0.0,
          0.0f, 0.0f, 0.0f, 50.0f, 100.0f, 16, true, {0.95f, 0.85f, 0.30f} },
    };
    return table;
}

} // namespace multi_window_levels
