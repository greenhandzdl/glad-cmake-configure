#ifndef GLDX_CAMERA_ORBITCAMERA_H
#define GLDX_CAMERA_ORBITCAMERA_H

/**
 * @file OrbitCamera.h
 * @brief A camera that orbits a point: yaw/pitch/distance + the input glue.
 *
 * ---- 为什么需要它 ----
 *
 * 19 个 demo 各自写了同一段东西：`float yaw, pitch, radius;` 三个数、鼠标拖拽时
 * `yaw -= dx * 0.006f`、滚轮时 `radius *= 0.9f`、以及把球坐标换成 eye 的那四行
 * 三角函数。抄到第 19 遍时它们已经开始**不一致**了——灵敏度有 0.005/0.006/0.01
 * 三种，pitch 的夹紧有的是 ±1.45 有的是 ±1.5，滚轮有乘法也有加法，个别 demo 忘了
 * 在 resize 时更新 aspect。这些差异都不是"配置"，是复制粘贴的产物。
 *
 * `gldx::Camera` 本身是刻意的纯数学类（view/projection 矩阵，不含输入），
 * 所以这里不把它改胖，而是**包一层**：`OrbitCamera` 持有 `Camera` 并拥有那三个
 * 轨道参数与输入换算。两者正交——要自由飞行就继续直接用 `Camera`（voxel 那种）。
 *
 * ---- 它不管什么 ----
 *
 * 不做指针捕获、不订阅窗口回调、不读键盘。输入落地的方式各 demo 不同（有的要
 * 排除 HUD 拖拽、有的只在捕获时响应），所以这里只提供**意图级的三个动作**
 * `Drag/Scroll/SetTarget`，由 demo 决定何时调用。这条边界是有意的：把
 * `glfwSetCursorPosCallback` 拉进来就等于把 gldxwin 依赖塞进引擎数学层。
 */

#include <glm/glm.hpp>

#include "gldx/camera/Camera.h"

namespace gldx {

/**
 * 轨道相机参数。全部是"人眼可理解"的量，不是矩阵。
 */
struct OrbitSettings {
    /** 水平角，弧度。0 = 从 +Z 方向看向目标（摄像机在目标正后方）。 */
    float yaw = 0.7f;
    /** 俯仰角，弧度。正 = 从上往下看。会夹到 `pitchLimit`。 */
    float pitch = 0.3f;
    /** 眼睛到目标的距离，米。会夹到 [`minDistance`, `maxDistance`]。 */
    float distance = 8.0f;
    /** 观察点（轨道中心）。 */
    glm::vec3 target{0.0f, 0.9f, 0.0f};

    /** pitch 的夹紧范围，弧度。默认 ±1.45（约 ±83°），避免在正上/正下方翻转。 */
    float pitchLimit = 1.45f;
    float minDistance = 0.4f;
    float maxDistance = 200.0f;

    /** 拖拽灵敏度：每像素多少弧度。 */
    float dragSensitivity = 0.006f;
    /** 滚轮缩放：每格乘多少。用乘法而不是加法，远处才不至于一格跳太远。 */
    float zoomFactor = 0.9f;
};

/**
 * 一台绕点旋转的相机：`OrbitSettings` + `Camera`，并负责把设置变成矩阵。
 *
 * 典型用法（每帧）：
 *
 *     orbit.Drag(cursorDelta);            // 鼠标拖了
 *     orbit.Scroll(wheelSteps);           // 滚了滚轮
 *     orbit.Apply(fovDegrees, aspect);    // 算出 view/projection
 *     render.camera = &orbit.camera();
 */
class OrbitCamera {
public:
    OrbitCamera() = default;
    explicit OrbitCamera(const OrbitSettings& settings) : settings_(settings) {}

    /** 鼠标拖拽。`deltaPixels` 是自上一帧起的位移（窗口坐标，y 向下）。 */
    void Drag(const glm::vec2& deltaPixels) {
        settings_.yaw -= deltaPixels.x * settings_.dragSensitivity;
        settings_.pitch = ClampPitch(settings_.pitch + deltaPixels.y * settings_.dragSensitivity);
    }

    /** 滚轮。`steps` 是格数（向上滚为正 = 拉近）。 */
    void Scroll(float steps) {
        settings_.distance = ClampDistance(settings_.distance * std::pow(settings_.zoomFactor, steps));
    }

    /**
     * 把当前设置算成 view/projection 写进内部 `Camera`。
     *
     * 单独一个函数而不是在访问器里惰性算：aspect 每帧可能变（resize），
     * 惰性算会漏掉"设置没变但视口变了"这一种情况。
     */
    void Apply(float fovYDegrees, float aspect, float nearZ = 0.1f, float farZ = 500.0f) {
        const float cp = std::cos(settings_.pitch);
        const glm::vec3 eye(settings_.target.x + settings_.distance * cp * std::sin(settings_.yaw),
                            settings_.target.y + settings_.distance * std::sin(settings_.pitch),
                            settings_.target.z + settings_.distance * cp * std::cos(settings_.yaw));
        camera_.SetPerspective(fovYDegrees, aspect, nearZ, farZ);
        camera_.LookAt(eye, settings_.target, glm::vec3(0.0f, 1.0f, 0.0f));
        camera_.SetOrbitRadius(settings_.distance);
    }

    [[nodiscard]] Camera& camera() noexcept { return camera_; }
    [[nodiscard]] const Camera& camera() const noexcept { return camera_; }

    [[nodiscard]] OrbitSettings& settings() noexcept { return settings_; }
    [[nodiscard]] const OrbitSettings& settings() const noexcept { return settings_; }

    // 常用量的直接访问器，省得每个调用方都写 `orbit.settings().yaw`。
    [[nodiscard]] float yaw() const noexcept { return settings_.yaw; }
    [[nodiscard]] float pitch() const noexcept { return settings_.pitch; }
    [[nodiscard]] float distance() const noexcept { return settings_.distance; }
    [[nodiscard]] const glm::vec3& target() const noexcept { return settings_.target; }

    /** 眼睛的世界坐标（供 `--target-y`、拾取、调试绘制用）。 */
    [[nodiscard]] glm::vec3 eye() const noexcept { return camera_.Position(); }

    void SetTarget(const glm::vec3& t) { settings_.target = t; }
    void SetDistance(float d) { settings_.distance = ClampDistance(d); }
    void SetYawPitch(float yawRad, float pitchRad) {
        settings_.yaw = yawRad;
        settings_.pitch = ClampPitch(pitchRad);
    }

private:
    [[nodiscard]] float ClampPitch(float p) const {
        const float lim = settings_.pitchLimit;
        return std::clamp(p, -lim, lim);
    }
    [[nodiscard]] float ClampDistance(float d) const {
        return std::clamp(d, settings_.minDistance, settings_.maxDistance);
    }

    OrbitSettings settings_{};
    Camera        camera_{};
};

} // namespace gldx

#endif // GLDX_CAMERA_ORBITCAMERA_H
