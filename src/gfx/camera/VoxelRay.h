#ifndef GFX_CAMERA_VOXELRAY_H
#define GFX_CAMERA_VOXELRAY_H

/**
 * @file VoxelRay.h
 * @brief Grid traversal raycast (Amanatides & Woo DDA) over a voxel volume.
 *
 * Header-only, pure math, no global state — the same properties that make the
 * picking helpers safe to call from any thread. The query is a template on a
 * solidity predicate so this stays independent of any particular world
 * storage: pass a Chunk, a whole chunk grid, or a lambda over a flat array.
 *
 * The classic "step into the next cell along the axis whose plane is nearest"
 * loop walks cell by cell in O(steps through the volume) with no per-cell
 * branching on the ray origin sign beyond the initial setup, which is exactly
 * what block picking in a voxel game needs: cheap enough for one ray per
 * mouse event, and stable enough to be numerically self-checkable.
 *
 * Reference: Amanatides & Woo, "A Fast Voxel Traversal Algorithm for Ray
 * Tracing", Eurographics 1987.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include <glm/glm.hpp>

#include "gfx/camera/Picking.h"   // Ray

namespace gfx {

// A hit carries the cell that stopped the ray plus the face entered through
// (unit normal pointing back toward the ray origin), so callers can place a
// new block at position + normal without a second query.
struct VoxelHit {
    glm::ivec3 position{0, 0, 0};
    glm::ivec3 normal{0, 0, 0};
    float      distance = 0.0f;   // along the ray from Ray::origin
};

// Walk the [min, max] inclusive cell range with ray `ray`, stopping at the
// first cell where solidity(cell) is true or after maxDist world units.
//
// min/max are cell coordinates (not world AABB corners); for a single 16^3
// chunk they are (0,0,0) and (kChunkSize-1) each, for a world the grid bounds.
// Cells outside the range are treated as void: the ray leaves the volume and
// reports a miss, which keeps a chunk-boundary edit from picking a phantom
// block in a neighbouring chunk the caller did not intend to query.
//
// A ray with any non-finite component (a NaN from an unprojected degenerate
// matrix, an inf from a camera that blew up) is a miss rather than a crash: the
// walk has no honest answer for it, and callers already handle nullopt.
template <typename SolidityFn>
inline std::optional<VoxelHit> RaycastVoxel(const Ray& ray,
                                            const glm::ivec3& min,
                                            const glm::ivec3& max,
                                            SolidityFn solidity,
                                            float maxDist = 64.0f) {
    glm::ivec3 cell;
    glm::vec3  tMax;    // distance to the next cell boundary per axis
    glm::ivec3 step;    // +1 / -1 traversal direction per axis
    glm::vec3  tDelta;  // distance between consecutive boundaries per axis

    const glm::vec3 o = ray.origin;
    const glm::vec3 d = ray.dir;

    // The setup below turns world coordinates into cell indices, and a float to
    // int conversion is undefined once the value leaves the int32 range. Ray
    // usually comes from PickRay unprojecting a view-projection, so a degenerate
    // matrix (or a camera that blew up) can hand this function a NaN or a 1e30
    // component. Answer "miss" instead of reading it as an arbitrary cell.
    if (!(std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z))
        || !(std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z)))
        return std::nullopt;

    // Written as `!(maxDist > 0)` so a NaN reach rejects itself instead of
    // turning every later "travelled > maxDist" comparison into a no.
    if (!(maxDist > 0.0f)) return std::nullopt;

    for (int axis = 0; axis < 3; ++axis) {
        const float oA = axis == 0 ? o.x : (axis == 1 ? o.y : o.z);
        const float dA = axis == 0 ? d.x : (axis == 1 ? d.y : d.z);
        const int   lo = axis == 0 ? min.x : (axis == 1 ? min.y : min.z);
        const int   hi = axis == 0 ? max.x : (axis == 1 ? max.y : max.z);

        if (std::fabs(dA) < 1e-8f) {
            // Parallel to this axis's planes: must start inside the slab or the
            // ray never crosses the volume.
            if (oA < static_cast<float>(lo) || oA > static_cast<float>(hi) + 1.0f)
                return std::nullopt;
            // Travel stays in the cell the origin falls in for the whole walk.
            // Starting at `lo` instead would follow a different column
            // altogether: an exactly level crosshair (dir.y == 0) would pick
            // the bottom layer of the volume rather than the block aimed at.
            cell[axis] = std::clamp(static_cast<int>(std::floor(oA)), lo, hi);
            tMax[axis] = std::numeric_limits<float>::max();
            tDelta[axis] = 0.0f;
            step[axis] = 0;
            continue;
        }

        const float inv = 1.0f / dA;
        // Find the first cell in the double domain and cast only once the value
        // is known to sit inside the queried range. An origin far outside the
        // volume is legitimate -- the ray just has to travel further -- but
        // (int)floor(1e30f), and the `lo - cell` skip that used to follow it,
        // are both undefined. The closed-form tMax below equals the old
        // "compute, then add (lo - cell) * tDelta" for every in-range origin.
        if (dA > 0.0f) {
            step[axis] = 1;
            tDelta[axis] = inv;
            const double first = std::floor(static_cast<double>(oA));
            if (first < static_cast<double>(lo)) {
                tMax[axis] = static_cast<float>((static_cast<double>(lo) + 1.0
                                                 - static_cast<double>(oA)) * static_cast<double>(inv));
                cell[axis] = lo;
            } else {
                if (first > static_cast<double>(hi)) return std::nullopt;
                cell[axis] = static_cast<int>(first);
                tMax[axis] = static_cast<float>((first + 1.0 - static_cast<double>(oA))
                                                * static_cast<double>(inv));
            }
        } else {
            step[axis] = -1;
            tDelta[axis] = -inv;
            const double first = std::ceil(static_cast<double>(oA)) - 1.0;
            if (first > static_cast<double>(hi)) {
                tMax[axis] = static_cast<float>((static_cast<double>(oA) - static_cast<double>(hi))
                                                * static_cast<double>(-inv));
                cell[axis] = hi;
            } else {
                if (first < static_cast<double>(lo)) return std::nullopt;
                cell[axis] = static_cast<int>(first);
                tMax[axis] = static_cast<float>((static_cast<double>(oA) - first)
                                                * static_cast<double>(-inv));
            }
        }
        if (cell[axis] < lo || cell[axis] > hi) return std::nullopt;
    }

    // If the origin sits inside a solid cell, report it immediately with no
    // face (a placement against "nothing" is the caller's problem to ignore).
    if (solidity(cell)) return VoxelHit{cell, glm::ivec3{0, 0, 0}, 0.0f};

    float travelled = 0.0f;
    glm::ivec3 lastStep{0, 0, 0};

    for (int guard = 0; guard < 4096; ++guard) {
        // Advance along the axis whose next boundary is closest.
        int axis = 0;
        if (tMax.y < tMax.x) axis = 1;
        if (tMax.z < tMax[axis]) axis = 2;

        travelled = tMax[axis];
        if (travelled > maxDist) return std::nullopt;

        cell[axis] += step[axis];
        lastStep[axis] = -step[axis];   // entered through the face we came from
        tMax[axis] += tDelta[axis];

        // Leaving the queried volume along any axis is a miss; the range check
        // is per-axis so a diagonal exit stops at the boundary cell.
        for (int a = 0; a < 3; ++a) {
            const int lo = a == 0 ? min.x : (a == 1 ? min.y : min.z);
            const int hi = a == 0 ? max.x : (a == 1 ? max.y : max.z);
            if (cell[a] < lo || cell[a] > hi) return std::nullopt;
        }

        if (solidity(cell)) return VoxelHit{cell, lastStep, travelled};
    }
    return std::nullopt;   // guard against pathological inputs (NaN dir etc.)
}

} // namespace gfx

#endif // GFX_CAMERA_VOXELRAY_H
