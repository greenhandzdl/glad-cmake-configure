/**
 * @file ProceduralRun.h
 * @brief 程序化奔跑：与行走共用骨架观测，但支撑相完全不同。
 *
 * ---- 跑步不是"更快的走路" ----
 *
 * 走路时永远至少有一只脚在地上；跑步有**腾空相**——双脚同时离地。这不是把
 * 步幅调大就能得到的：它要求支撑相从 60% 降到 35% 左右，两条腿的支撑区间不再
 * 相接，中间留下一段真空。
 *
 * 于是这条曲线与 `ProceduralWalk` 的差别是**结构上的**，不是数值上的：
 *
 *   |<--- stance 0.35 --->|<------ flight+swing ------>|
 *   触地            蹬离     ← 腾空 → 摆腿 → 下一次触地
 *
 * 所以它是独立类型而不是 `WalkTuning` 的一个参数。硬把两者揉成一个类型，就得
 * 在 `LegAt` 里塞一堆 `if (isRun)`，而那正是"两套曲线逐渐发散"的开端。
 *
 * ---- 一个必须承认的近似 ----
 *
 * 腾空期间**整具身体应当离开地面**，而腾空高度在这里没有被驱动：本类只负责
 * 姿态（腿的收放）。腾空高度要么由调用方从骨盆轨迹反解，要么（更常见）用
 * `AirborneWeightAt(t)` 作为掩码，把 `GroundContact` 的结果按权重衰减掉——
 * 跑步的腾空只有 0.1 秒左右，人的眼睛读的是"脚离地了没有"，不是一个精确的
 * 抛物线。这与跳跃不同：跳跃是一整段明确的腾空，那里值得走真抛物线。
 */

#ifndef GLDX_ANIM_PROCEDURALRUN_H
#define GLDX_ANIM_PROCEDURALRUN_H

#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

/**
 * 奔跑的可调量。默认值对 1.6-1.8 m 的人形大致成立，比行走的那组更"通用"——
 * 跑步的姿态容差更大（没有裙摆这种第三方约束）。
 */
struct RunTuning {
    // 支撑相占比。**这是跑步与走路最本质的一个数**：0.35 意味着一条腿只在
    // 周期的 35% 里承重，两条腿的支撑区间之间留出 30% 的腾空。
    //
    // 调到 0.5 会得到"竞走"——看起来在跑，但永远有一只脚粘在地上，总觉得哪里不对。
    // 调到 0.6 就是走路的形状了。
    float stanceEnd = 0.35f;

    float hipSwing        = 0.62f;   // 大腿摆幅，约走路的 1.9 倍
    float kneeStanceFlex  = 0.35f;   // 触地缓冲，比走路深得多（跑步落地冲击大）
    float kneeSwingFlex   = 1.45f;   // 摆动相峰值屈膝，脚跟几乎碰到臀部
    float kneeFlightFlex  = 0.55f;   // 腾空相后腿的收膝（前脚伸出、后脚收起）
    float ankleHeelStrike = -0.10f;  // 跑步多是**中足/前足**落地，背屈比走路小
    float ankleToeOff     = 0.55f;   // 蹬伸更狠
    float toeOffBend      = 0.50f;

    // 上身前倾：跑步比走路前倾得多，且这个前倾是"读起来像在跑"的主要线索之一。
    float lean = 0.22f;              // rad

    // 摆臂：跑步的肘屈曲几乎固定在大约 90°（走路是随摆动变化的），且摆幅更大。
    float armSwing = 0.75f;
    float elbowHold = 1.30f;         // rad，接近 90° 的固定屈肘

    float hipYaw   = 0.14f;
    float chestYaw = 0.22f;
    float headYaw  = 0.06f;

    // 手臂下放的基准角：跑步时上臂本来就贴着身体，不需要走路那么多 A-pose。
    float armDown = 0.75f;
};

/**
 * 程序化奔跑。循环，一个 Duration 是一个完整双步周期。
 */
class ProceduralRun final : public AnimPoseSource {
public:
    ProceduralRun(const Skeleton& skeleton, const RigProfile& rig,
                  const std::vector<glm::mat4>& restWorld, const RunTuning& tuning,
                  float cycleSeconds);

    [[nodiscard]] const char* name() const noexcept override { return "run"; }
    [[nodiscard]] float Duration() const noexcept override { return duration_; }
    [[nodiscard]] bool Looping() const noexcept override { return true; }

    // 触地那一刻。状态机从走切到跑时等在这里，脚步才不会错乱。
    [[nodiscard]] float ContactPhase() const noexcept override { return 0.0f; }

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    /**
     * 时刻 t 的"腾空权重"：0 = 双脚着地（应当正常做地面解算），1 = 完全腾空
     * （应当完全不解算）。
     *
     * 为什么是连续权重而不是布尔：跑步的腾空边界每帧都在移动，布尔量会在边界
     * 那一帧让骨盆高度突变（表现为"跳一下"）。用权重衰减则是一条连续的过渡。
     *
     * 这是 `ContactPhase()` 之外多出来的一个查询，因为跑步有**两段**腾空（左右腿
     * 各一段），一个标量相位说不清。
     */
    [[nodiscard]] float AirborneWeightAt(float t) const noexcept;

    [[nodiscard]] const RunTuning& tuning() const noexcept { return tuning_; }

private:
    const Skeleton&               skeleton_;
    const RigProfile&             rig_;
    const std::vector<glm::mat4>& restWorld_;
    RunTuning                     tuning_;
    float                         duration_ = 1.0f;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALRUN_H
