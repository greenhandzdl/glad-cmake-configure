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
 * 姿态（腿的收放）。`AirborneWeightAt(t)` 是给调用方的连续权重，用它可以做两件事：
 *
 *  1. 把 `GroundContact` 的抬升按 `1 - weight` 衰减（权重为 1 时完全不贴地）；
 *  2. **往上加腾空高度**。这一步很容易漏：只衰减不加，角色的脚在腾空帧会被解算
 *     钉在地板上，人却在空中折腿——看着像悬着一根看不见的绳子。正确写法是
 *     `高度 = (1 - weight) * 解算值 + weight * (站立高度 + 腾空高度)`，
 *     与跳跃用的是同一个公式（见 demo 的 VerticalShiftFor）。
 *
 * 权重的形状是有讲究的：**值和斜率在腾空边界都为 0**（这里用 `sin²` 而不是
 * `sin`）。它乘的是地面解算的抬升量，所以斜率不为 0 意味着腾空开始那一帧骨盆被
 * 硬拽走几厘米——实测跑步起跳帧骨盆突变 5.9 cm，看起来像脚被地板弹了一下。
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

    float hipSwing        = 0.79f;   // 真人跑步约 45 deg（原来 0.62 = 35.5，只有 79%）
    float kneeStanceFlex  = 0.52f;   // 真人约 30 deg（原来 0.35 = 20，只有 67%）
    float kneeSwingFlex   = 1.92f;   // 真人约 110 deg（原来 1.45 = 83，只有 76%）
    float kneeFlightFlex  = 0.55f;   // 腾空相后腿的收膝（前脚伸出、后脚收起）
    float ankleHeelStrike = -0.10f;  // 跑步多是**中足/前足**落地，背屈比走路小
    float ankleToeOff     = 0.55f;   // 蹬伸更狠
    float toeOffBend      = 0.50f;

    // 上身前倾：跑步比走路前倾得多，且这个前倾是"读起来像在跑"的主要线索之一。
    float lean = 0.22f;              // rad

    // 摆臂：跑步的肘屈曲几乎固定在大约 90°（走路是随摆动变化的），且摆幅更大。
    float armSwing = 0.96f;   // 真人约 55 deg
    float elbowHold = 1.30f;         // rad，接近 90° 的固定屈肘

    float hipYaw   = 0.14f;
    float chestYaw = 0.22f;
    float headYaw  = 0.06f;

    // 手臂下放的基准角：跑步时上臂本来就贴着身体，不需要走路那么多 A-pose。
    // 手臂从 bind pose 放到身侧的**剩余角度**，不是"从 T-pose 放下来多少"。
    //
    // 0.790 是在本仓的参考资产（MMD/PMX，240 骨）上量出来的：它的 bind pose 是
    // A-pose，上臂已经有 fwd +0.02 / up -1.85 / side +1.87，即水平下方 44.8°；
    // 到垂直还剩 45.2° = 0.790 rad。
    //
    // 这里曾经是 1.05（为另一具 T-pose 骨架定的）。那是把 A-pose 的手臂再转 60°，
    // 结果为水平下方 105°——**越过垂直线又绕回身体另一侧**。症状很迷惑：参数确实
    // 生效了，但把值从 0.85 扫到 1.30，手腕到中线的横向距离只变了 2.7 cm，看起来
    // 像"参数没接上"。所以这个数必须按 bind pose 实际朝向算，不能跨资产照抄。
    //
    // bind pose 不是 A-pose 的资产，应当在自己的 tuning 里覆盖这一项。
    float armDown = 0.790f;
    // Upper-arm abduction, same role as WalkTuning::armOut: with the arms hanging
    // straight down the hands sweep through the skirt. 0.40 rad was picked by eye
    // against the reference skirt (0.22 still intersected, 0.55 read as "arms held up").
    float armOut = 0.0f;

    // 腾空高度（米）。这是**世界单位**，不在姿态里用——调用方拿 `AirborneWeightAt`
    // 乘在垂直位移上（见 ProceduralRun.h 文件头）。12 cm 是慢跑的量级：抬得再高
    // 就成跨栏了，而 0 会得到"两只脚在地上跳"。
    float airbornePeak = 0.12f;
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

    /**
     * 时刻 t 的腾空高度（米），已经乘过权重。调用方把 `站立高度 + 它` 当作这一帧的
     * 整具角色高度，其余部分与跳跃完全一致（见 demo 的 VerticalShiftFor）。
     */
    [[nodiscard]] float airborneRiseAt(float t) const noexcept {
        return tuning_.airbornePeak * AirborneWeightAt(t);
    }

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
