/**
 * @file ProceduralIdle.h
 * @brief 待机：站着，但活着。呼吸 + 重心微晃 + 视线的极缓慢游移。
 *
 * ---- 为什么这是整套动作里最该有的一个 ----
 *
 * 其它动作都是"用户要求时发生"的；这个动作是**默认**——角色不在走、不在跑、
 * 不在攻击时，它在播这个。少了它，任何一帧没人按键盘的画面都是一具 T-pose 或
 * 一具完全静止的雕像，而"完全静止"与"卡死"在人眼里是同一件事。
 *
 * ---- 幅度小到什么程度才对 ----
 *
 * 三处运动都刻意做到"看不出是动画"：呼吸是胸腔 ±0.018 rad，重心左右是 ±0.010 rad，
 * 视线游移是 ±0.04 rad 且周期十几秒。判据不是"看不见"，而是**去掉它之后画面变死**。
 * 这个区别很容易搞反——把幅度调大到能一眼看出在动，人就会觉得角色在发抖或喝醉了。
 *
 * ---- 与蹲伏的区别 ----
 *
 * `ProceduralCrouch` 也是"原地轻微运动"，但它有明确的姿态（深屈膝、躯干前倾）。
 * 待机是直立基准。两者共用的只有"呼吸"这个想法，数值与关节完全不同，所以是
 * 两个类型——抽象成"带呼吸的动作基类"只会让两个都变难读。
 */

#ifndef GLDX_ANIM_PROCEDURALIDLE_H
#define GLDX_ANIM_PROCEDURALIDLE_H

#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

struct IdleTuning {
    // 一次呼吸的长度。人静息时约 3-4 秒，所以 3.6 秒接近真实；做成参数是因为
    // 同一段待机放在大个子身上会显得急促。
    float breathSeconds = 3.6f;

    // 胸腔起伏幅度。呼吸的主体是胸廓，不是整条脊柱——只转 spine 会让整个上身
    // 像一块板在前后点，转 chest 才有"胸腔撑开"的感觉。
    float breathChest = 0.018f;   // rad
    float breathSpine = 0.007f;   // rad，脊柱只跟一点点

    // 重心微晃。周期是呼吸的两倍，所以两个运动不会同拍（同拍会读成一个动作）。
    // **三个子周期必须保持整比**（breath : sway : gaze = 1 : 2 : 4），否则
    // Duration() 与 Looping() 的承诺就不成立——见类注释。
    float swayAmplitude = 0.010f; // rad
    float swaySeconds   = 7.2f;

    // 视线的极缓慢游移。头部小幅转动，周期十几秒——短了像在左右张望，那是
    // 另一个动作（"警觉"），不是待机。
    float gazeAmplitude = 0.040f; // rad
    float gazeSeconds   = 14.4f;

    // 手臂：先下放成 A-pose。这是 T-pose 骨架的必需品——不放下手臂，待机看起来
    // 就是"举着双手站着"，而那读起来像投降而不是放松。
    float armDown    = 1.10f;  // rad
    float elbowBend  = 0.18f;  // rad，几乎伸直，只留一点自然弯
    float armOut     = 0.06f;  // rad，离开躯干，免得手臂穿进大衣

    // 站姿：膝盖留一点微屈。完全锁死的直腿（bind pose 就是）看起来像立正，
    // 而正常人站着时膝关节始终有一点点弯。
    float stanceKnee = 0.045f;  // rad
};

/**
 * 待机。
 *
 * ---- 三个子周期必须整拍复合，不能"各转各的" ----
 *
 * 呼吸 3.6 s、重心 7.2 s、视线 14.4 s —— 彼此是 **1 : 2 : 4** 的整数比，所以
 * 14.4 秒之后三条曲线同时回到起点，`Duration()` 就是 14.4 秒（= 视线的一整圈，
 * 也是呼吸的四圈、重心的两圈）。构造函数会把三个子周期**就地吸附**到这个整拍
 * 网格上（按呼吸周期取整），所以即使调用方填了任意数，循环性仍然精确。
 * 这一点是硬要求，不是审美选择：
 *
 * `Looping()` 返回 true 就承诺了 `Sample(t)` 与 `Sample(t + Duration())` 给出同一
 * 姿态。早先版本让三者互不同拍（3.6 / 7.2 / 13.0），于是 t+Duration 时呼吸归位、
 * 重心与视线还停在半途，实测漂移 1.8e-2——比其它循环动作的 1e-7 大五个数量级。
 * 那既破坏了循环语义，也让"按周期截帧做回归"变成不可能。改成整比后漂移
 * 降到 3.7e-9，与 walk/run/crouch 同量级。
 *
 * 想要呼吸与摆动不同拍以免读成一个动作，靠的是**整比而不是无公倍数**：
 * 1:2 已经让两个运动在一圈内错开四次，足够避免同拍感。
 */
class ProceduralIdle final : public AnimPoseSource {
public:
    ProceduralIdle(const Skeleton& skeleton, const RigProfile& rig,
                   const std::vector<glm::mat4>& restWorld, const IdleTuning& tuning);

    [[nodiscard]] const char* name() const noexcept override { return "idle"; }

    /**
     * 完整一圈：视线的整周期（= 呼吸的 3 圈 = 重心的 2 圈的一半…… 见构造函数）。
     * 由三个子周期复合而来，所以改动任何一个子周期都会改变它。
     */
    [[nodiscard]] float Duration() const noexcept override { return duration_; }
    [[nodiscard]] bool Looping() const noexcept override { return true; }

    // 没有落脚瞬间。
    [[nodiscard]] float ContactPhase() const noexcept override { return -1.0f; }

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    [[nodiscard]] const IdleTuning& tuning() const noexcept { return tuning_; }

private:
    const Skeleton&               skeleton_;
    const RigProfile&             rig_;
    const std::vector<glm::mat4>& restWorld_;
    IdleTuning                    tuning_;
    float                         duration_ = 1.0f;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALIDLE_H
