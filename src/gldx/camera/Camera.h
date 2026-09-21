#ifndef GLDX_CAMERA_CAMERA_H
#define GLDX_CAMERA_CAMERA_H

/**
 * @file Camera.h
 * @brief Perspective camera (view + projection). Pure math, no GL.
 *
 * Read-only accessors return cached matrices, so a Camera can be safely shared
 * with worker threads as long as it is only mutated on the render thread.
 */

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace gldx {

class Camera {
public:
    // Which projection maps view space -> clip space. Toggled at runtime (Tab).
    enum class Projection { Perspective, Orthographic };

    Camera() = default;

    void SetPerspective(float fovYDegrees, float aspect, float nearZ, float farZ);
    // worldHeight is the vertical extent of the view box at the target plane;
    // the horizontal extent follows from `aspect` so the box keeps the ratio.
    void SetOrthographic(float worldHeight, float aspect, float nearZ, float farZ);

    // Switch between the two projections, keeping the stored near/far and a
    // framing that tracks the orbit radius. Returns the active projection.
    Projection ToggleProjection();

    void LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up);
    void SetTransform(const glm::vec3& eye, const glm::quat& orientation);

    void SetViewportAspect(float aspect);

    // Orbit helpers (used by the demo).
    void Orbit(float yawDeltaRad, float pitchDeltaRad);
    void Dolly(float distanceDelta);

    // Free-fly / first-person controls (voxel demo). Orthogonal to the orbit
    // helpers: both mutate the same orientation_ + position_, so a demo can
    // toggle between the two models without rebuilding the camera.
    //
    // Angles are radians with the classic voxel-game convention: yaw 0 looks
    // down -Z, positive yaw turns left, positive pitch looks up (clamped to
    // +/-90 degrees by the caller, who owns the sensitivity curve). Distances
    // are world units — speed * dt stays in the game layer, because only it
    // knows whether this frame is a walk, a fly or a teleport.
    void SetYawPitch(float yawRad, float pitchRad);
    [[nodiscard]] glm::vec2 YawPitch() const;
    [[nodiscard]] glm::vec3 Forward() const;
    [[nodiscard]] glm::vec3 Right() const;
    [[nodiscard]] glm::vec3 Up() const;
    void MoveForward(float distance);
    void MoveRight(float distance);
    void MoveUp(float distance);
    void Translate(const glm::vec3& worldDelta);

    [[nodiscard]] const glm::mat4& ViewMatrix()        const noexcept { return view_; }
    [[nodiscard]] const glm::mat4& ProjectionMatrix()  const noexcept { return proj_; }
    [[nodiscard]] const glm::mat4& ViewProjection()    const noexcept { return viewProj_; }
    [[nodiscard]] const glm::vec3& Position()          const noexcept { return position_; }

    // Perspective parameters + inverse view-projection (needed by CSM splits).
    [[nodiscard]] float NearPlane() const noexcept { return near_; }
    [[nodiscard]] float FarPlane()  const noexcept { return far_; }
    // Vertical field of view in degrees; lets callers turn a world-space size
    // into pixels (particle point size) without duplicating the projection.
    [[nodiscard]] float FovY()      const noexcept { return fovY_; }
    [[nodiscard]] glm::mat4 InverseViewProjection() const;

    [[nodiscard]] Projection projectionType() const noexcept { return projType_; }

    // Matrix for drawing the skybox: always a translation-stripped *perspective*
    // projection so the unit cube fills the screen at depth 1 regardless of the
    // scene's projection mode (the xyww trick relies on a non-constant w, which
    // an orthographic box would break). A background cube has no scale cue, so
    // this stays visually correct even when the scene itself is orthographic.
    [[nodiscard]] glm::mat4 SkyboxViewProj() const;

    // Distance from the eye to the orbit target; used to size the ortho box so
    // an ortho view frames the same content the perspective view does.
    void SetOrbitRadius(float radius) { orbitRadius_ = radius; }

private:
    void RecomputeView();
    void RecomputeProjection();

    glm::mat4 proj_{1.0f};
    glm::mat4 view_{1.0f};
    glm::mat4 viewProj_{1.0f};

    glm::vec3 position_{0.0f, 0.0f, 5.0f};
    glm::quat orientation_{1.0f, 0.0f, 0.0f, 0.0f};

    float fovY_ = 45.0f;
    float aspect_ = 1.0f;
    float near_ = 0.1f;
    float far_ = 100.0f;

    Projection projType_ = Projection::Perspective;
    float orthoHeight_ = 12.0f;   // vertical view-box extent (world units)
    float orbitRadius_ = 11.0f;    // demo hint for ortho framing
};

} // namespace gldx

#endif // GLDX_CAMERA_CAMERA_H
