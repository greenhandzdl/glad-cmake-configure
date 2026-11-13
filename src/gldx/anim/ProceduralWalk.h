/**
 * @file ProceduralWalk.h
 * @brief 程序化行走：一条相位曲线撑起来的步态，不依赖任何动画资产。
 *
 * ---- 为什么这算"引擎层的东西" ----
 *
 * 具体的**数值**是在 anim-rigid 那具穿裙子的角色上试出来的（裙摆会把小臂吞掉，
 * 所以 armDown 是 1.05 而不是 1.45），换个角色必然要重调。但**曲线的形状**是通用
 * 的人形知识：支撑相占 60%、摆动相膝盖必须早抬、踝关节在蹬离后解旋。这些换十具
 * 骨架都成立。
 *
 * 所以分法是：形状进引擎，数值由调用方给（`WalkTuning`），默认值就是那具角色的
 * 调好值。想让新角色不穿裙子，改几个数就行，不用复制一份 LegAt()。
 *
 * ---- 一个必须知道的限制 ----
 *
 * 这套东西**只对直立的人形骨架成立**。它假设了左右两条腿各挂在骨盆下面、以及
 * "绕侧向轴转 = 前后摆"这个前提（`RigProfile` 已经量好了侧向轴，所以这一点是
 * 由分类保证的，不是假设）。四足、多臂、反向关节的骨架不该用这个类。
 */

#ifndef GLDX_ANIM_PROCEDURALWALK_H
#define GLDX_ANIM_PROCEDURALWALK_H

#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

/**
 * 行走的全部可调量。默认值来自 anim-rigid 的参考资产（1.64 m、23 骨、裙装）。
 *
 * 单位一律是**弧度**，不是角度：这些数直接进 `RotateBone`，中途换单位是这类代码
 * 最常见的静默错误（角度当弧度传，旋转小 57 倍，看起来像"动作没生效"）。
 */
struct WalkTuning {
    // 一条腿在一个周期里"着地承重"的那一段占比。0.6 是真人步行的值；两条腿因此
    // 重叠 20%——那段双脚同时着地的时段（双支撑相）就是"走路"看起来像走路而不是
    // 像跑步的原因。调到 0.5 会得到一个没有过渡、每一步都像换脚的怪步态。
    float stanceEnd = 0.60f;

    float hipSwing        = 0.33f;   // 大腿从最前到最后
    float kneeStanceFlex  = 0.14f;   // 刚落地那一下的缓冲
    float kneeSwingFlex   = 0.72f;   // 摆动相峰值屈膝（决定脚抬多高）
    float ankleHeelStrike = -0.18f;  // 落地时背屈（脚尖朝上）
    float ankleToeOff     = 0.34f;   // 蹬离时跖屈（脚尖朝下）
    float toeOffBend      = 0.34f;   // 脚跟离地、只剩脚尖着地时趾关节的背屈

    // T-pose -> A-pose。1.05 是渲染特写逐步试出来的：1.45（83°）时腕子明显陷进
    // 裙摆，1.05（60°）时袖口、手掌、四根手指在**每个相位**都离裙边约 0.1 m。
    // 再往下就等于把手臂留在 T-pose：轮廓看着平静，网格互相穿插。
    float armDown   = 1.05f;         // rad
    float armSwing  = 0.38f;
    float elbowBase = 0.55f;         // 两个摆动相都保留的屈肘
    float elbowSwing = 0.22f;        // 手臂前摆时额外加的
    float armOut    = 0.05f;         // 前臂外展：靠它把小臂拿出裙摆，而不是靠更屈肘
    float wristBase = 0.14f;
    float wristSwing = 0.16f;
    float wristDeviate = 0.07f;
    float clavicleRoll = 0.045f;     // 肩胛跟着手臂微转

    float hipYaw    = 0.10f;         // 骨盆朝迈步腿那一侧转
    float chestYaw  = 0.15f;         // 肩反向转
    float lean      = 0.06f;         // 恒定前倾
    float headYaw   = 0.05f;         // 头大致保持朝前

    // 蹲下行走（0 = 站直）。**这是行走的变体，不是另一个动作**：蹲行和站立行走
    // 用的是同一条步态曲线，只是步幅收窄、膝盖常态屈曲、骨盆下沉。做成两套曲线
    // 之后必然有一天会不一致。
    //
    // 这里只影响**姿态**（腿的弯法）；骨盆到底降多少由调用方从地面接触反解
    // （见 GroundContact）——手写一个摇摆动只会跟其它每一处腿部改动打架。
    float crouch = 0.0f;             // 0..1
};

/**
 * 程序化步态。`Duration()` 由调用方给出（一步有多久），因为那个数来自步幅与
 * 速度的关系，属于"这具角色走多快"而不是"步态长什么样"。
 */
class ProceduralWalk final : public AnimPoseSource {
public:
    /**
     * @param skeleton  / rig / restWorld 由调用方持有，必须比本对象活得久。
     * @param cycleSeconds 一个完整双步周期的秒数（左腿一次完整往返）。<= 0 会被
     *        夹到 1e-3 并可能让 `Duration()` 变成无意义的小数；调用方应当从
     *        `4 * legLength * sin(hipSwing)` 这类关系推出来（见 demo）。
     */
    ProceduralWalk(const Skeleton& skeleton, const RigProfile& rig,
                   const std::vector<glm::mat4>& restWorld, const WalkTuning& tuning,
                   float cycleSeconds);

    [[nodiscard]] const char* name() const noexcept override { return "walk"; }
    [[nodiscard]] float Duration() const noexcept override { return duration_; }
    [[nodiscard]] bool Looping() const noexcept override { return true; }

    // 双支撑相的中点：两条腿都在地上，是切出/切入行走最不容易看出接缝的时刻。
    [[nodiscard]] float ContactPhase() const noexcept override { return 0.0f; }

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    [[nodiscard]] const WalkTuning& tuning() const noexcept { return tuning_; }

private:
    const Skeleton&                skeleton_;
    const RigProfile&              rig_;
    const std::vector<glm::mat4>&  restWorld_;
    WalkTuning                     tuning_;
    float                          duration_ = 1.0f;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALWALK_H
