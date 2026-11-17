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
    // 用的是同一条步态曲线，只是步幅收窄、膝盖常态屈曲，于是整个人矮下去。做成
    // 两套曲线之后必然有一天会不一致。
    //
    // 这里只影响**姿态**（腿的弯法）；骨盆实际落多高由调用方从地面接触反解
    // （见 GroundContact）——手写一个高度只会跟其它每一处腿部改动打架。
    float crouch = 0.0f;             // 0..1

    // ---- 蹲行的比例 ----
    //
    // 蹲行不是"把膝盖弯一点"。`crouch` 是**比例**，真正决定姿态的是下面两条：
    //   1. `knee = 2 * hip`（在 ProceduralWalk.cpp 里写死）：大腿与小腿等长时，
    //      这条比例把踝关节放在髋正下方，骨盆因此落在脚上方而不是脚后方 0.3 m。
    //   2. `strideScale()`：蹲着的人迈不了大步，而地面滚动是按站立的 cycleMetres
    //      算的，所以调用方要把地面同步缩到这个比例，否则脚会滑。
    //
    // 这里**没有**"骨盆下沉多少"这种参数：手写一个下沉量会被地面解算原样抵消
    // （实测，见 ProceduralWalk.cpp 里那段注释）。身高是靠上面的比例降下去的。

    // ---- 蹲行的几何：全是实测出来的三个数 ----
    //
    // 绕 `rig.side` 的正向让朝下的骨头往**后**摆，所以**髋前屈是负、膝屈是正**。
    // 同号会把腿折成一侧：膝盖跑到髋后面、脚跑到身体前面。
    float crouchHip  = 0.72f;        // rad（大小；施加时取负）
    // `knee = 2 * hip`：大腿与小腿等长时，这条比例把踝关节放到髋正下方，骨盆才蹲在
    // 脚上方。其它比例会让骨盆停到脚后；躯干随后被迫前折去补，就成了"探着身子"。
    float crouchKnee = 1.44f;        // rad，= 2 * crouchHip
    // 踝抵消小腿的世界倾角 `-(hip + knee)`，脚掌才平。漏掉时只有鞋跟着地，地面
    // 解算会把整具踮起的角色抬起来——实测 0.24 m，"怎么调都不蹲"就是这么来的。
    //
    // 每次循环在地上铺的那一段，相对站立行走的比例。蹲行 0.5——步子小一半。
    // 调用方要拿它同步"喂给 Sample 的相位"和"地面滚动量"，否则脚会滑（见
    // ProceduralWalk::strideScale）。
    [[nodiscard]] float strideScale() const noexcept { return 1.0f - 0.50f * crouch; }
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

    /**
     * 按**相位**（0..1）摆这次姿态，不经时间折叠。
     *
     * 这条接口存在的理由只有一个：`ProceduralCrouch` 要用 `crouch = 1` 的那条曲线
     * 在**固定相位**上取一帧，画成"静止蹲姿"。那样蹲下与蹲走之间就没有第二套常量、
     * 也没有切换跳变——"蹲下 = 蹲走的某一帧"是可验证的，不是一句口号。
     *
     * 相位必须是 0..1 内已折好的数；它不循环。
     */
    void SampleAtPhase(float phase, std::vector<glm::mat4>& outPose) const;

    [[nodiscard]] const WalkTuning& tuning() const noexcept { return tuning_; }

    /**
     * 这个步态每次循环在地上铺多长的一段，相对站立行走的比例（蹲行 = 0.5）。
     *
     * 调用方**必须**用它同步两件事，否则脚会滑：喂给 `Sample` 的相位，以及地面
     * 的滚动量。蹲着走步子小，而地面是按站立的 `cycleMetres` 滚的，两者不同步的
     * 表现是"脚在原地划"——而且它不会报错，只能靠量（demo 的 pose check 有一列
     * `stride` 专门盯它，测出来的脚掌跨度小于期望值 75% 就点名 SKATES）。
     */
    [[nodiscard]] float strideScale() const noexcept { return tuning_.strideScale(); }

private:
    const Skeleton&                skeleton_;
    const RigProfile&              rig_;
    const std::vector<glm::mat4>&  restWorld_;
    WalkTuning                     tuning_;
    float                          duration_ = 1.0f;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALWALK_H
