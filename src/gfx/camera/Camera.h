#ifndef GFX_CAMERA_CAMERA_H
#define GFX_CAMERA_CAMERA_H

/**
 * @file Camera.h
 * @brief Perspective camera (view + projection). Pure math, no GL.
 *
 * Read-only accessors return cached matrices, so a Camera can be safely shared
 * with worker threads as long as it is only mutated on the render thread.
 */

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace gfx {

class Camera {
public:
    Camera() = default;

    void SetPerspective(float fovYDegrees, float aspect, float nearZ, float farZ);

    void LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up);
    void SetTransform(const glm::vec3& eye, const glm::quat& orientation);

    void SetViewportAspect(float aspect);

    // Orbit helpers (used by the demo).
    void Orbit(float yawDeltaRad, float pitchDeltaRad);
    void Dolly(float distanceDelta);

    [[nodiscard]] const glm::mat4& ViewMatrix()        const noexcept { return view_; }
    [[nodiscard]] const glm::mat4& ProjectionMatrix()  const noexcept { return proj_; }
    [[nodiscard]] const glm::mat4& ViewProjection()    const noexcept { return viewProj_; }
    [[nodiscard]] const glm::vec3& Position()          const noexcept { return position_; }

private:
    void RecomputeView();

    glm::mat4 proj_{1.0f};
    glm::mat4 view_{1.0f};
    glm::mat4 viewProj_{1.0f};

    glm::vec3 position_{0.0f, 0.0f, 5.0f};
    glm::quat orientation_{1.0f, 0.0f, 0.0f, 0.0f};

    float fovY_ = 45.0f;
    float aspect_ = 1.0f;
    float near_ = 0.1f;
    float far_ = 100.0f;
};

} // namespace gfx

#endif // GFX_CAMERA_CAMERA_H
