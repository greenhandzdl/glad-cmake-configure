#ifndef GLDX_VOXEL_COLLISION_H
#define GLDX_VOXEL_COLLISION_H

/**
 * @file Collision.h
 * @brief Axis-aligned body movement through a voxel world, with wall slide
 *        (pure CPU, header-only, no GL, no allocation).
 *
 * The picking and raycast helpers answer "what does this ray hit"; this answers
 * the gameplay question a voxel engine ultimately needs: "where may this body
 * move". A player is modelled as an axis-aligned box standing on its `pos`
 * (feet) point, and every frame the game proposes a displacement; this resolves
 * it against the solid cells so the body comes to rest flush against terrain
 * instead of tunnelling through it or sticking to the nearest integer.
 *
 * The resolver moves one axis at a time and snaps the body back to the exact
 * cell face it crossed. That ordering is what produces the sliding feel: walking
 * into a wall at an angle cancels only the blocked axis, so the parallel axis
 * still carries the body along the surface. There is no swept-AABB / contact-time
 * math because voxel faces are axis-aligned unit squares — the per-axis snap is
 * already exact as long as a single move does not advance the body by more than
 * one cell on an axis (a fast-moving caller should sub-step; see MoveVoxelAabb).
 *
 * Like the rest of the pure-CPU voxel layer this is a template on a solidity
 * predicate: give it a lambda over a chunk grid, a whole world, or a flat array,
 * and it stays independent of any particular storage.
 */

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

namespace gldx {

// The moving body as an axis-aligned box. `pos` (passed to MoveVoxelAabb) is the
// CENTRE of the box's BOTTOM face — the feet — so the box spans
//   [pos.x - radius, pos.x + radius] x [pos.y, pos.y + height] x
//   [pos.z - radius, pos.z + radius]
// in world units. `radius` is a half-extent (a 0.6-wide player uses 0.3), and
// `height` runs from the feet up, matching how a first-person eye sits on top of
// a body rather than at its centre.
struct VoxelBody {
    float radius = 0.3f;   // half-extent in X and Z (must be >= 0)
    float height = 1.8f;   // feet to head (must be > 0)
};

// Which axes of a move were blocked. `grounded` is the flag a platformer gates
// gravity and jumping on: it is set only when a downward (-Y) move was stopped,
// so an upward move that bonks a ceiling reports hitY without being "on ground".
struct VoxelMoveResult {
    bool hitX = false;
    bool hitY = false;
    bool hitZ = false;
    bool grounded = false;
};

// Does the box anchored at `pos` overlap any cell where solid(cell) is true?
// Exposed for callers that need a spawn/validity test ("is this player inside
// geometry?") separate from movement. Only cells the box can touch are probed,
// so a 0.6-wide, 1.8-tall body asks at most 2x2x2 = 8 cells.
template <class SolidFn>
inline bool VoxelAabbSolid(const glm::vec3& pos, const VoxelBody& body, SolidFn solid) {
    constexpr float kEps = 1e-4f;
    // Half-open box [min, max): the upper bound pulls back by kEps so a face
    // resting exactly on a cell boundary counts as TOUCHING that cell, not
    // overlapping it. The snapping in MoveVoxelAabb leaves the body a hair clear
    // of the wall it stopped on, and this is what lets that flush-resting body
    // still slide along the wall instead of the touching cell re-blocking the
    // parallel axis.
    const int x0 = static_cast<int>(std::floor(pos.x - body.radius));
    const int x1 = static_cast<int>(std::floor(pos.x + body.radius - kEps));
    const int y0 = static_cast<int>(std::floor(pos.y));
    const int y1 = static_cast<int>(std::floor(pos.y + body.height - kEps));
    const int z0 = static_cast<int>(std::floor(pos.z - body.radius));
    const int z1 = static_cast<int>(std::floor(pos.z + body.radius - kEps));
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
                if (solid(glm::ivec3(x, y, z))) return true;
    return false;
}

// Move the body by `delta`, resolving against cells where solid(cell) is true.
// `pos` is updated in place to the resting feet position; the return value
// reports which axes were blocked. X and Z resolve before Y so that a body that
// both slides along a wall and lands on the floor this frame ends up flush on
// both contacts rather than trading one for the other.
//
// `solid` is called with integer cell coordinates and must return true for
// anything that blocks the body. Cells outside the caller's world should report
// as solid (or the body walks off the edge of the data); the resolver itself
// never bounds-checks, it only asks.
//
// The per-axis snap assumes |delta| moves the body less than one cell on any
// axis — a physical frame step at sane speeds always satisfies this. A caller
// teleporting a body several cells (a respawn, a fast projectile) should split
// the move into sub-steps of at most ~0.5 cells each and call this per sub-step;
// a single oversized delta may stop short of, rather than through, a thin wall.
template <class SolidFn>
inline VoxelMoveResult MoveVoxelAabb(glm::vec3& pos, const VoxelBody& body,
                                     glm::vec3 delta, SolidFn solid) {
    constexpr float kEps = 1e-4f;
    VoxelMoveResult res;

    // --- X: push out to the face of the cell the leading edge entered --------
    pos.x += delta.x;
    if (delta.x != 0.0f && VoxelAabbSolid(pos, body, solid)) {
        pos.x = delta.x > 0.0f
                    ? std::floor(pos.x + body.radius) - body.radius - kEps
                    : std::floor(pos.x - body.radius) + 1.0f + body.radius + kEps;
        res.hitX = true;
    }

    // --- Z: same resolution on the other ground axis -------------------------
    pos.z += delta.z;
    if (delta.z != 0.0f && VoxelAabbSolid(pos, body, solid)) {
        pos.z = delta.z > 0.0f
                    ? std::floor(pos.z + body.radius) - body.radius - kEps
                    : std::floor(pos.z - body.radius) + 1.0f + body.radius + kEps;
        res.hitZ = true;
    }

    // --- Y: floor and ceiling, resolved last so it lands on the slid XZ -----
    pos.y += delta.y;
    if (delta.y != 0.0f && VoxelAabbSolid(pos, body, solid)) {
        if (delta.y > 0.0f) {
            pos.y = std::floor(pos.y + body.height) - body.height - kEps;
        } else {
            pos.y = std::floor(pos.y) + 1.0f + kEps;
            res.grounded = true;
        }
        res.hitY = true;
    }

    return res;
}

} // namespace gldx

#endif // GLDX_VOXEL_COLLISION_H
