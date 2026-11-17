/**
 * @file ProceduralCrouch.h
 * @brief 静止蹲姿 = 蹲走**循环里的某一帧**，不是另一套曲线。
 *
 * ---- 这条关系是硬要求，不是风格偏好 ----
 *
 * 上一版里 `ProceduralCrouch` 与 `ProceduralWalk::crouch` 各写一份常量。结果两边
 * 慢慢分叉：蹲下的头高 1.097 m、蹲走 1.112 m，脚部位置差 2.5 cm——按 `4` 切到蹲走
 * 时脚在地上蹭一下，反过来像踩空。任何"两个动作看起来应该连续"的地方，两套常量
 * 迟早会给出两个答案。
 *
 * 所以本类**不再自己摆腿**：它持有一个 `WalkTuning{crouch = 1}` 的步态曲线，把相位
 * 钉在 `kCrouchPhase`（0.25，双支撑相的中点：两条腿都在地上，是蹲姿最稳的一帧），
 * 然后只叠一层呼吸。于是"蹲下 = 蹲走的某帧"这条性质由构造保证，可以逐字节验证。
 *
 * ---- 为什么相位偏偏是 0.25 ----
 *
 * 0.0 也位于双支撑相（两条腿都着地），但那里的腿是一条前一条后——蹲着摆成那样看起来
 * 像"要站起来"；0.25 处两条大腿对称、膝盖都朝前，才是参考图里那种"蹲着等"的样子。
 * 而重心稳定靠的不是这个数：它靠 `knee = 2*hip` 那条比例让踝关节落在髋正下方（推导见
 * ProceduralCrouch.cpp），以及地面解算每帧把最低鞋底钉回地板——两条腿本来就都在地上，
 * 所以这个动作里根本没有"重心飘"的自由度。
 *
 * ---- 呼吸只动上身 ----
 *
 * 胸腔起伏、重心微摆、头轻微转动。**腿一动不动**：腿一动脚就会离开地面，而蹲下的脚
 * 一旦离开地面，地面解算就会把整具角色抬起来——那正是"重心不稳"的观感来源。
 */

#ifndef GLDX_ANIM_PROCEDURALCROUCH_H
#define GLDX_ANIM_PROCEDURALCROUCH_H

#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

struct CrouchTuning {
    // 蹲多深。0 = 站着（这个动作退化成静止站姿），1 = 全蹲。
    // 它是**姿态**的深度，不是骨盆高度——后者由地面解算给出。
    float depth = 1.0f;

    // 相位钉在步态循环的哪里。
    //
    // **必须是双支撑相（0 或 0.5），不能是中点 0.25**：走路的两条腿永远相差半个
    // 周期，所以"两腿完全对称"的那一帧并不存在；能同时踩住地的只有双支撑相。
    // 实测 0.25（一条腿在支撑中期、另一条在摆动中期）会让一条腿在后侧抬起来——
    // 那是一个"单脚站着、另一只脚悬空"的姿态，重心根本不在支撑面里，看起来像
    // 要倒。0 处左腿脚跟着地、右腿支撑末期，两只脚都实打实踩在地上。
    //
    // 0 与 0.5 是镜像；取 0（左腿在前）与 demo 里 `--action crouchwalk` 的起点一致，
    // 于是"按 3 与按 4 的第一帧"是同一个姿态。
    float posePhase = 0.0f;

    // 呼吸。周期独立于蹲姿本身，所以它是一个常驻的轻微运动。**只动上身**：
    // 腿一动脚就离地，地面解算随后把整具角色抬起来，那才是"重心不稳"的来源。
    float breathSeconds = 3.2f;   // 一次呼吸
    float breathSpine   = 0.022f; // rad，胸腔起伏的幅度
    float breathSway    = 0.010f; // rad，极缓的左右重心摆动
    float breathHead    = 0.030f; // rad，头随呼吸轻轻抬落
};

/**
 * 原地蹲伏。循环（呼吸是周期的），`ContactPhase()` 为 -1：蹲着的时候没有
 * "落脚瞬间"这个概念，状态机不该为了等它而挂起。
 */
class ProceduralCrouch final : public AnimPoseSource {
public:
    ProceduralCrouch(const Skeleton& skeleton, const RigProfile& rig,
                     const std::vector<glm::mat4>& restWorld, const CrouchTuning& tuning,
                     const WalkTuning& walkTuning);

    [[nodiscard]] const char* name() const noexcept override { return "crouch"; }
    [[nodiscard]] float Duration() const noexcept override { return tuning_.breathSeconds; }
    [[nodiscard]] bool Looping() const noexcept override { return true; }

    // 继承默认的 -1（"本动作没有落脚瞬间"）。显式写出来是为了让读代码的人
    // 知道这是决定，不是漏写。
    [[nodiscard]] float ContactPhase() const noexcept override { return -1.0f; }

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    /** 本动作钉在蹲走循环的哪个相位（见 CrouchTuning::posePhase）。 */
    [[nodiscard]] float PosePhase() const noexcept { return tuning_.posePhase; }

    [[nodiscard]] const CrouchTuning& tuning() const noexcept { return tuning_; }

private:
    const Skeleton&               skeleton_;
    const RigProfile&             rig_;
    const std::vector<glm::mat4>& restWorld_;
    CrouchTuning                  tuning_;
    // 蹲走的曲线本身。持有它是本类的全部意义：腿的角度来自同一条曲线，而不是
    // 第二份常量（见文件头）。
    ProceduralWalk                pose_;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALCROUCH_H
