#ifndef GFX_CAMERA_FRUSTUM_H
#define GFX_CAMERA_FRUSTUM_H

/**
 * @file Frustum.h
 * @brief Six-plane view frustum for conservative culling (plan "Frustum").
 *
 * Pure math, read-only once extracted from a view-projection matrix, so it can
 * be handed to worker threads without synchronisation (plan section 1.2). The
 * six planes use inward-pointing unit normals (Gribb-Hartmann extraction from
 * the rows of clip = viewProj * worldPos), letting sphere / AABB tests reject
 * objects that lie entirely outside a single plane.
 */

#include <array>

#include <glm/glm.hpp>

namespace gfx {

class Frustum {
public:
    enum class Result { Outside, Intersect, Inside };

    // Build the six planes from a view-projection matrix (column-major glm).
    void Extract(const glm::mat4& viewProj);

    // Conservative sphere test (world-space centre + radius). Outside is only
    // returned when the sphere is fully beyond one plane.
    [[nodiscard]] Result IntersectSphere(const glm::vec3& center, float radius) const;

    // Axis-aligned box test: picks a "positive vertex" per plane.
    [[nodiscard]] Result IntersectAABB(const glm::vec3& mn, const glm::vec3& mx) const;

    // Convenience: true when the sphere is at least partly inside (Visible).
    [[nodiscard]] bool SphereVisible(const glm::vec3& center, float radius) const {
        return IntersectSphere(center, radius) != Result::Outside;
    }

    [[nodiscard]] const std::array<glm::vec4, 6>& planes() const noexcept { return planes_; }

private:
    // planes stored as (nx, ny, nz, d) with dot(n,p) + d > 0 meaning inside.
    std::array<glm::vec4, 6> planes_{};
};

} // namespace gfx

#endif // GFX_CAMERA_FRUSTUM_H
