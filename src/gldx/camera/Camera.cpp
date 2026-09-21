module;

#include "gldx/gmf.hpp"

#include <glm/gtc/matrix_inverse.hpp>

module gldx;

namespace gldx {

namespace {
// Rotate a vector by a quaternion using only the stable gtc API
// (glm::mat3_cast), avoiding the experimental GLM_GTX_quaternion extension.
inline glm::vec3 Rotate(const glm::quat& q, const glm::vec3& v) {
    return glm::mat3_cast(q) * v;
}
} // namespace

void Camera::SetPerspective(float fovYDegrees, float aspect, float nearZ, float farZ) {
    projType_ = Projection::Perspective;
    fovY_ = fovYDegrees;
    aspect_ = aspect;
    near_ = nearZ;
    far_ = farZ;
    RecomputeProjection();
}

void Camera::SetOrthographic(float worldHeight, float aspect, float nearZ, float farZ) {
    projType_ = Projection::Orthographic;
    orthoHeight_ = worldHeight;
    aspect_ = aspect;
    near_ = nearZ;
    far_ = farZ;
    RecomputeProjection();
}

Camera::Projection Camera::ToggleProjection() {
    if (projType_ == Projection::Perspective) {
        // Match the perspective framing: the ortho box height equals the world
        // height visible at the orbit target for the current fov + radius.
        const float h = 2.0f * orbitRadius_ * std::tan(glm::radians(fovY_) * 0.5f);
        SetOrthographic(h, aspect_, near_, far_);
    } else {
        SetPerspective(fovY_, aspect_, near_, far_);
    }
    return projType_;
}

void Camera::RecomputeProjection() {
    if (projType_ == Projection::Orthographic) {
        const float hh = orthoHeight_ * 0.5f;
        const float hw = hh * aspect_;
        proj_ = glm::ortho(-hw, hw, -hh, hh, near_, far_);
    } else {
        proj_ = glm::perspective(glm::radians(fovY_), aspect_, near_, far_);
    }
    viewProj_ = proj_ * view_;
}

void Camera::SetViewportAspect(float aspect) {
    aspect_ = aspect;
    RecomputeProjection();
}

void Camera::RecomputeView() {
    const glm::vec3 fwd = Rotate(orientation_, glm::vec3(0.0f, 0.0f, -1.0f));
    const glm::vec3 up  = Rotate(orientation_, glm::vec3(0.0f, 1.0f, 0.0f));
    view_ = glm::lookAt(position_, position_ + fwd, up);
    viewProj_ = proj_ * view_;
}

void Camera::LookAt(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up) {
    position_ = eye;
    view_ = glm::lookAt(eye, target, up);
    orientation_ = glm::quat_cast(glm::inverse(view_));
    viewProj_ = proj_ * view_;
}

void Camera::SetTransform(const glm::vec3& eye, const glm::quat& orientation) {
    position_ = eye;
    orientation_ = glm::normalize(orientation);
    RecomputeView();
}

void Camera::Orbit(float yawDeltaRad, float pitchDeltaRad) {
    orientation_ = glm::normalize(
        glm::angleAxis(yawDeltaRad, glm::vec3(0.0f, 1.0f, 0.0f)) * orientation_);
    const glm::vec3 right = Rotate(orientation_, glm::vec3(1.0f, 0.0f, 0.0f));
    orientation_ = glm::normalize(orientation_ * glm::angleAxis(pitchDeltaRad, right));
    RecomputeView();
}

void Camera::Dolly(float distanceDelta) {
    const glm::vec3 fwd = Rotate(orientation_, glm::vec3(0.0f, 0.0f, -1.0f));
    position_ += fwd * distanceDelta;
    RecomputeView();
}

void Camera::SetYawPitch(float yawRad, float pitchRad) {
    // Rebuild the orientation from the two angles rather than accumulating
    // quaternion products: repeated small-angle multiplications drift off the
    // unit sphere and (worse) let a rolled frame accumulate over a long mouse
    // session. Yaw is applied around world Y on top of the local pitch, which
    // is the convention that keeps horizontal looking free of roll.
    orientation_ = glm::normalize(
        glm::angleAxis(yawRad, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::angleAxis(pitchRad, glm::vec3(1.0f, 0.0f, 0.0f)));
    RecomputeView();
}

glm::vec2 Camera::YawPitch() const {
    // Invert the forward vector of the yaw*pitch decomposition (see the
    // SetYawPitch comment for the derivation):
    //   fwd = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch))
    const glm::vec3 fwd = Forward();
    const float pitch = std::asin(glm::clamp(fwd.y, -1.0f, 1.0f));
    // At +/-90 degrees cos(pitch) -> 0 and yaw becomes ill-defined; returning
    // the last finite atan2 answer is fine because the caller only uses this
    // for incremental mouse deltas.
    const float yaw = std::atan2(-fwd.x, -fwd.z);
    return {yaw, pitch};
}

glm::vec3 Camera::Forward() const {
    return Rotate(orientation_, glm::vec3(0.0f, 0.0f, -1.0f));
}

glm::vec3 Camera::Right() const {
    return Rotate(orientation_, glm::vec3(1.0f, 0.0f, 0.0f));
}

glm::vec3 Camera::Up() const {
    return Rotate(orientation_, glm::vec3(0.0f, 1.0f, 0.0f));
}

void Camera::MoveForward(float distance) {
    position_ += Forward() * distance;
    RecomputeView();
}

void Camera::MoveRight(float distance) {
    position_ += Right() * distance;
    RecomputeView();
}

void Camera::MoveUp(float distance) {
    position_ += Up() * distance;
    RecomputeView();
}

void Camera::Translate(const glm::vec3& worldDelta) {
    position_ += worldDelta;
    RecomputeView();
}

glm::mat4 Camera::InverseViewProjection() const {
    return glm::inverse(viewProj_);
}

glm::mat4 Camera::SkyboxViewProj() const {
    const glm::mat4 persp = glm::perspective(glm::radians(fovY_), aspect_, near_, far_);
    return persp * glm::mat4(glm::mat3(view_));
}

} // namespace gldx
