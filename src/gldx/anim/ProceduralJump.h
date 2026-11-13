/**
 * @file ProceduralJump.h
 * @brief 程序化跳跃：起跳 / 腾空 / 落地三段，没有一条关键帧。
 *
 * ---- 与前一段动作最大的区别：地面接触必须关掉 ----
 *
 * 行走时 `GroundContact` 每帧把骨盆往上抬/往下放，让鞋底刚好贴地——这是对的，
 * 因为走路本来就一直有一只脚在地上。跳跃不是：腾空期间脚**本来就不该在地上**，
 * 若继续解算，它会算出"最低的鞋底在 -0.4 m"这种结论并坚持把角色抬起来，于是
 * 角色在空中的每一帧都被拽回地面附近，看起来像被一根看不见的绳子吊着。
 *
 * 所以 `GroundedAt(t)` 是接口的一部分：调用方每帧问一句"这一刻该不该做地面解算"。
 * 把它留在实现里（而不是让调用方自己按 t 判断）是因为**只有实现知道哪几段是腾空**，
 * 外面重新划一次区间就是第二份真值。
 *
 * ---- 腾空高度是抛物线，不是正弦 ----
 *
 * 重力下抛体的高度是 h(t) = v0*t - g*t^2/2。用正弦去凑"跳起来"能看，但上升和下降
 * 的形状是反的（正弦两端平、中间陡；抛体两端陡、顶点平），而人眼对这种差异很敏感：
 * 直觉上"跳"的顶点应该有个短暂的悬停感，且落地那一刻速度最大。所以这里走真抛物线。
 */

#ifndef GLDX_ANIM_PROCEDURALJUMP_H
#define GLDX_ANIM_PROCEDURALJUMP_H

#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

/**
 * 一次跳跃的全部可调量。时间线固定为三段，比例由这三个时长给出：
 *
 *     |<-- crouch -->|<-- air -->|<-- land -->|
 *     下蹲蓄力        腾空         落地缓冲
 *
 * 时长(秒)而不是比例：跳一次多久是"这具角色怎么跳"，不该被别的动作的长度影响。
 */
struct JumpTuning {
    float crouchSeconds = 0.22f;   // 下蹲蓄力
    float airSeconds    = 0.55f;   // 离地到再次触地
    float landSeconds   = 0.26f;   // 落地屈膝缓冲

    float apexHeight    = 0.55f;   // 米，腾空最高点相对起跳姿态

    // 蓄力段：髋膝踝一起屈，骨盆下沉多少由调用方的地面解算给出（这里只负责弯腿）。
    float crouchKnee    = 0.85f;   // rad
    float crouchHip     = 0.45f;   // rad
    float crouchAnkle   = -0.35f;  // rad，背屈（脚尖翘起）

    // 腾空段：离地瞬间腿是蹬直的、手臂往上甩；过顶点后开始收腿准备落地。
    float airArmRaise   = 0.9f;    // rad，手臂上举
    float airTuckKnee   = 0.75f;   // rad，过顶点后收膝
    float airTuckHip    = -0.35f;  // rad，大腿前抬

    // 落地段：比蓄力更深一点，然后回到站直。冲击感来自这一段的总时长够短。
    float landKnee      = 1.05f;   // rad
    float landHip       = 0.55f;   // rad
    float landAnkle     = -0.30f;  // rad
};

/**
 * 一次跳跃。**不循环**：`Looping()` 为 false，所以 t 超过总时长后钳在最后一帧
 * （站直），而不是回绕到蓄力——回绕会让角色无限连跳，那不是"一次跳跃"。
 */
class ProceduralJump final : public AnimPoseSource {
public:
    ProceduralJump(const Skeleton& skeleton, const RigProfile& rig,
                   const std::vector<glm::mat4>& restWorld, const JumpTuning& tuning);

    [[nodiscard]] const char* name() const noexcept override { return "jump"; }
    [[nodiscard]] float Duration() const noexcept override { return duration_; }
    [[nodiscard]] bool Looping() const noexcept override { return false; }

    // 落地那一刻就是唯一可靠的接触相位——起跳前也在地面上，但那时还没离地。
    [[nodiscard]] float ContactPhase() const noexcept override;

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    /**
     * 时刻 t 是否"脚踩在地上"。调用方据此决定要不要跑 `GroundContact::Solve`：
     * 为 false 时必须跳过着地解算，否则角色会被拽回地面（见文件头）。
     */
    [[nodiscard]] bool GroundedAt(float t) const noexcept;

    /** 时刻 t 骨盆相对起跳姿态的额外抬升（米）。腾空段是抛物线，其余段是 0。 */
    [[nodiscard]] float AirborneRiseAt(float t) const noexcept;

    [[nodiscard]] const JumpTuning& tuning() const noexcept { return tuning_; }

private:
    const Skeleton&               skeleton_;
    const RigProfile&             rig_;
    const std::vector<glm::mat4>& restWorld_;
    JumpTuning                    tuning_;
    float                         duration_ = 1.0f;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALJUMP_H
