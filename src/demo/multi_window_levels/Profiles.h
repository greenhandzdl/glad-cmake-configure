#ifndef MULTI_WINDOW_LEVELS_PROFILES_H
#define MULTI_WINDOW_LEVELS_PROFILES_H

/**
 * @file Profiles.h
 * @brief The window "levels": one row per window, each a deliberately different
 *        combination of the four orthogonal multi-window dimensions.
 *
 * multi_viewport proves the *synchronised, same-scene, same-tier* corner of the
 * design space: every window samples one shared atomic clock and renders one
 * identical field, differing only by a fixed camera offset. This demo widens
 * that to the rest of the space the engine's layering allows, by giving every
 * window a Profile that varies four independent axes at once:
 *
 *   1. Clock       - Sync windows read the one SharedState atomic phase (so the
 *                    spinning cube shows the SAME angle in all of them every
 *                    frame); Async windows ignore it and accumulate their own
 *                    phase from per-frame dt * rate, so they visibly drift away
 *                    from the sync pair and from each other.
 *   2. Viewpoint   - a fixed yaw/pitch/radius per window. Different eye points
 *                    build different view frusta, so the SAME shared object
 *                    field culls to a DIFFERENT visible set per window.
 *   3. Reach / FOV - near/far plane and vertical field of view. A short far
 *                    plane or a tight fov drops more of the field behind/aside
 *                    the frustum: a literal, countable "this window shows it,
 *                    that one does not".
 *   4. Detail (LOD)- sphere tessellation per tier: the flagship draws smooth
 *                    high-segment spheres, the scout draws chunky low-poly ones.
 *
 * A fifth axis is reserved for the special "island" window (independent=true):
 * it shares NOTHING with the others - not the field, not the pipeline, not the
 * clock - it draws its own DebugDraw wireframe on its own context, to prove
 * two windows can be fully unrelated GPU-side while living in one process and
 * one render thread.
 *
 * Pure std + a few glm scalars; no GL, so the table is trivially CPU-side and
 * can be read from main before any window/context exists.
 */

#include <glm/glm.hpp>

#include <vector>

namespace multi_window_levels {

constexpr int    kMaxProfiles = 5;
constexpr float  kPi          = 3.14159265358979323846f;
constexpr double kSpinSpeed   = 1.6;   // rad of cube spin per elapsed second

// Where a window's animation phase comes from.
enum class Clock { Sync, Async };

struct Profile {
    const char* name   = "window";
    Clock       clock  = Clock::Sync;
    double      rate   = 1.0;   // Async only: dt multiplier (0.35 = slow-motion)
    double      start  = 0.0;   // Async only: initial private phase

    float yaw   = 0.9f;         // fixed orbit angles (no shared drag here)
    float pitch = 0.36f;
    float radius = 11.0f;       // eye distance from the field centre
    float fov   = 50.0f;        // vertical field of view, degrees
    float far   = 300.0f;       // view distance: the far plane culls beyond this

    int   lodSeg = 32;          // sphere segments/rings (detail tier)
    bool  independent = false;  // island window: render unrelated content

    glm::vec3 tint{0.9f, 0.5f, 0.25f};  // baseColor variant so tiers read apart
};

// The five graded windows, ordered flagship (highest tier) down to the island.
const std::vector<Profile>& ProfileTable();

} // namespace multi_window_levels

#endif // MULTI_WINDOW_LEVELS_PROFILES_H
