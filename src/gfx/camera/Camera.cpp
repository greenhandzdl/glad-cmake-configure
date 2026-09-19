module;

#include "gfx/gmf.hpp"

#include <glm/gtc/matrix_inverse.hpp>

module gfx;

namespace gfx {

namespace {
// Rotate a vector by a quaternion using only the stable gtc API
// (glm::mat3_cast), avoiding the experimental GLM_GTX_quaternion extension.
inline glm::vec3 Rotate(const glm::quat& q, const glm::vec3& v) {
    return glm::mat3_cast(q) * v;
}
} // namespace

void Camera::SetPerspective(float fovYDegrees, float aspect, float nearZ, float farZ) {
    fovY_ = fovYDegrees;
    aspect_ = aspect;
    near_ = nearZ;
    far_ = farZ;
    proj_ = glm::perspective(glm::radians(fovY_), aspect_, near_, far_);
    viewProj_ = proj_ * view_;
}

void Camera::SetViewportAspect(float aspect) {
    SetPerspective(fovY_, aspect, near_, far_);
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

glm::mat4 Camera::InverseViewProjection() const {
    return glm::inverse(viewProj_);
}

} // namespace gfx
