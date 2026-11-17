/**
 * @file ProceduralSwim.h
 * @brief 游泳：身体**水平浮在水面**，双臂交替前伸划水，腿在身后小幅打腿。
 *
 * ---- 整具身体是"躺着"的，而这件事不在本类里做 ----
 *
 * 姿态层只写**局部**矩阵，它没有世界。俯卧这件事必须绕世界侧向轴转、而且转的是
 * 根骨——所以它由调用方在拿到位姿之后做一次 `RotateBone(pose, hips, side, -角度)`，
 * 本类只负责"一个俯卧的人该有的关节角"。
 *
 * 换句话说：**不翻转的话，画出来就是个站着划手的人**。这条不是备注，是使用契约。
 *
 * ---- 一条会游泳的人长的什么样 ----
 *
 * 参考图（泳池里自由泳的实拍）里有四件事，缺一件就不像：
 *
 *  1. **身体是一条直线**：从指尖到脚背基本在同一个平面上，只有换气那一拍略微侧转。
 *     所以腿要**向后伸**（髋伸展），不能像走路那样垂着；脚背绷直。
 *  2. **一条手臂前伸**：在水里，前伸的那只手臂是"指向行进方向"的，肘几乎伸直。
 *  3. **另一条手臂划到体侧**：手掌从体侧推到大腿，屈肘约 100°，然后**高肘出水**，
 *     从空中前甩回前伸。
 *  4. **腿在身后小幅鞭状打腿**：幅度小（十几度）、频率是划水的两倍，膝盖只在上提
 *     那半段略弯。
 *
 * 水面高度也由调用方给（`WaterRise(m)`）：整具身体被抬到水线上，脚不再接地面。
 */

#ifndef GLDX_ANIM_PROCEDURALSWIM_H
#define GLDX_ANIM_PROCEDURALSWIM_H

#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

struct SwimTuning {
    // 一个完整周期（左右各划一次）的秒数。
    float cycleSeconds = 2.4f;

    // ---- 躯干（相对"已俯卧的身体"）----
    // 身体已经由调用方转成水平；这里只叠：
    float headLift   = 0.22f;    // rad，抬头（俯卧的人不抬头就是脸埋在水里）
    float spineArch  = -0.10f;   // rad，胸椎略挺（让背在水面上）
    float bodyRoll   = 0.22f;    // rad，绕前向轴的滚动：跟划水的那一侧走，换气时最大
    float headTurn   = 0.35f;    // rad，换气时头侧转（这具骨架没有独立颈骨，只能整头转）

    // ---- 腿：向后伸直 + 小幅打腿 ----
    // 髋的**世界角**：0 表示腿与躯干在同一条直线上。曾经把它当成"往后摆多少"，
    // 但从侧面看，正角是让腿从水里**翘上去**（用户看到的"腿太高"就是这个）。
    // 微小负值让脚尖略沉，符合游泳时腿略低于水面的样子。
    float legTrail   = -0.06f;   // rad，0 = 腿与躯干齐平
    float kickAmp    = 0.18f;    // rad，打腿幅度（小幅度、鞭状）
    float kneeFlex   = 0.26f;    // rad，打腿时膝的最大弯曲（只在下面那半段出现）
    float anklePoint = 0.45f;    // rad，脚背绷直

    // ---- 手臂 ----
    // 划水行程拆成四个数，它们共同决定"划水"而不是"抡胳膊"：
    float reachFront  = 1.55f;   // rad，前伸量（手臂越过身体中线指向行进方向）
    float pullBack    = 1.75f;   // rad，从最前划到体侧/大腿
    float elbowBase   = 0.12f;   // rad，前伸时的基础屈肘（几乎伸直）
    float elbowPull   = 1.45f;   // rad，推水时的屈肘峰值
    float elbowRecover = 0.55f;  // rad，空中移臂的屈肘
    float recoverLift = 0.45f;   // rad，高肘出水时手臂抬离水面
    float armAbduct   = 0.42f;   // rad，手臂横向裹水
    float wristSnap   = 0.35f;   // rad，推水末端扣腕

    // 手臂前后摆的**方向**（+1 或 -1）。这具骨架的 T-pose 手臂指向 +side，俯卧之后
    // "绕 up 正转"到底把左臂送到头前还是身后，取决于骨架手性——实测写错时两只手会
    // 摊到身体两侧（俯视 wSide 2.5 m、wFwd 只有 1.0 m）。
    float upSign = -1.0f;
};

/**
 * 游泳。循环，`ContactPhase()` 为 -1（水里没有落脚瞬间）。
 */
class ProceduralSwim final : public AnimPoseSource {
public:
    /**
     * 俯卧角（弧度）：让身体从"站着"转到"脸朝下"的那一次整体旋转。
     *
     * **它必须由这一层掌握**，不能由调用方在采样之后补：`Sample` 内部计算手臂的
     * 目标方向时用的是身体坐标系（forward/up），而调用方会在拿到位姿后再做一次
     * 俯卧旋转——两者不在同一个坐标系里，对齐出来的手臂方向就是错的。症状是
     * "手臂几乎不动"（实测两帧之间只有 1% 像素变化），因为目标方向与静止方向
     * 几乎平行，`acos(dot)` 永远得到一个小角度。
     */
    void SetProneAngle(float radians) { prone_ = radians; }
    [[nodiscard]] float proneAngle() const noexcept { return prone_; }

    ProceduralSwim(const Skeleton& skeleton, const RigProfile& rig,
                   const std::vector<glm::mat4>& restWorld, const SwimTuning& tuning);

    [[nodiscard]] const char* name() const noexcept override { return "swim"; }
    [[nodiscard]] float Duration() const noexcept override { return duration_; }
    [[nodiscard]] bool Looping() const noexcept override { return true; }
    [[nodiscard]] float ContactPhase() const noexcept override { return -1.0f; }

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    [[nodiscard]] const SwimTuning& tuning() const noexcept { return tuning_; }

    /**
     * 这一刻身体相对"站立高度"要抬多少米，才能让躯干浮在水面附近。
     *
     * 俯卧之后身体离地很低（髋在 0.6 m 上下），而游泳的人躯干是在**水线**上的。
     * 调用方把这个数加到垂直位移里（与跳跃的抛物线走同一条路），脚就不再接地面。
     * 取常数：游泳不是跳跃，身体在水里是稳定漂浮的，起伏应当来自划水而不是从这
     * 里再生成一条曲线。
     */
    [[nodiscard]] float waterRise() const noexcept { return waterRise_; }
    void SetWaterRise(float m) noexcept { waterRise_ = m; }

private:
    const Skeleton&               skeleton_;
    const RigProfile&             rig_;
    const std::vector<glm::mat4>& restWorld_;
    SwimTuning                    tuning_;
    float                         duration_ = 1.0f;
    float                         prone_ = 0.0f;   // see SetProneAngle
    // 米，见 waterRise()。
    //
    // 游泳的人躯干**在水线附近**，腿略沉——也就是重心比"平躺在半空"低。所以这里
    // 用**负**值把身体从俯卧后的自然高度往下压：俯卧之后髋在 1.19 m，压 0.42 m
    // 到 0.77 m 左右，胸口贴着水线、腿在水下。此前给的是 +0.09 m（往上抬），
    // 结果是整个人浮在水面之上，俯视时先看到的是脸和胸——看起来像仰泳，而实际
    // 身体是面朝下的。
    float                         waterRise_ = -0.42f;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALSWIM_H
