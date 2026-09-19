#ifndef GFX_SCENE_TRANSFORM_H
#define GFX_SCENE_TRANSFORM_H

/**
 * @file Transform.h
 * @brief Translation/rotation/scale component of a scene node (plan "Transform").
 *
 * Pure CPU math, no GL. A Transform is just data plus a local-matrix builder, so
 * a whole scene graph made of Transforms can be advanced on any thread; only the
 * resulting world matrices are later consumed by render-thread draw calls. GLM
 * stores matrices column-major, and a quaternion rotation keeps the shear-free
 * TRS decomposition that scene hierarchies rely on.
 */

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace gfx {

struct Transform {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};   // identity (w, x, y, z)
    glm::vec3 scale{1.0f};

    Transform() = default;
    Transform(glm::vec3 t, glm::quat r, glm::vec3 s) noexcept
        : translation(std::move(t)), rotation(std::move(r)), scale(std::move(s)) {}

    // Build the node's local (parent-space) matrix: T * R * S.
    [[nodiscard]] glm::mat4 LocalMatrix() const noexcept {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), translation);
        m *= glm::mat4_cast(rotation);
        m = glm::scale(m, scale);
        return m;
    }

    // Convenience: this transform applied under a parent's world matrix.
    [[nodiscard]] glm::mat4 WorldFrom(const glm::mat4& parentWorld) const noexcept {
        return parentWorld * LocalMatrix();
    }

    // Set rotation from an axis + angle (radians); keeps the quaternion unit.
    void SetAxisAngle(const glm::vec3& axis, float radians) noexcept {
        rotation = glm::angleAxis(radians, glm::normalize(axis));
    }
};

// Largest column length of a transform's linear part - approximates how much a
// local-space bounding sphere grows once the (rigid + uniform-ish scale) matrix
// is applied. Shared by the scene graph and the picking / culling paths.
inline float MaxAxisScale(const glm::mat4& m) noexcept {
    auto len = [&m](int c) { return std::sqrt(m[c].x * m[c].x + m[c].y * m[c].y + m[c].z * m[c].z); };
    return std::max({len(0), len(1), len(2)});
}

} // namespace gfx

#endif // GFX_SCENE_TRANSFORM_H
