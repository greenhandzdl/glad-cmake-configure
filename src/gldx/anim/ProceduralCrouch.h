/**
 * @file ProceduralCrouch.h
 * @brief 蹲下：静止的蹲伏姿态，带一点呼吸，不是"蹲着走"。
 *
 * ---- 为什么蹲着走不在这里 ----
 *
 * "蹲着走路"是 `ProceduralWalk` 的一个参数（`WalkTuning::crouch`），因为它用的
 * 是同一条步态曲线，只是步幅收窄、膝盖常态屈曲。把它做成这里的第二个动作，
 * 就得到两套会各自演化的腿部曲线。
 *
 * 这里做的是**另一种东西**：原地蹲伏。它没有周期性的迈步，姿态基本是静态的，
 * 但完全静止会读作"暂停中的模型"而不是"蹲着的人"。所以加了一点呼吸——胸腔
 * 起伏与极缓的重心摆动，幅度小到看不出是动画，但去掉它整个画面就死了。
 *
 * ---- 骨盆高度从哪来 ----
 *
 * 与行走一样**不在这里写**：本类只负责关节角（膝盖弯多少、髋屈多少），
 * 骨盆实际降到多低由调用方从地面接触反解（`GroundContact::Solve`）。蹲姿的
 * 膝盖弯得很深，如果这里再写一个"骨盆下沉 0.35 m"，两处会打架——而打架的结果
 * 是脚陷进地面或者悬空，且没有任何报错。
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

    float kneeBend = 0.62f;   // rad，与 hipBend 相等 => 小腿垂直、膝盖在脚正上方（参考蹲姿）
    float hipBend  = 0.62f;   // rad，髋前屈（与 kneeBend 相等时小腿垂直）
    float ankleBend = 0.00f;  // rad，小腿垂直时脚掌本来就是平的

    // 骨盆向前移动多少（骨骼空间单位）。**这是唯一能移动重心的东西**：本函数其余
    // 全部是旋转，而旋转搬不动质心——屈髋只是转大腿，前倾只是绕一个本来就在脚后方
    // 的关节转上半身，脊柱倾角加到 48° 头依然落在脚跟后面（截图就是这样：脚踩实了，
    // 整个人往后倒）。真人蹲下是先把臀往后送、再把上身挪回脚上方，在刚性骨架里就是
    // 骨盆的平移。
    float forwardShift = 0.0f;

    // 骨盆下沉多少（骨骼空间单位）。**屈膝本身不会让身体变矮**——它只是把腿折起来，
    // 而地面解算随后把最低点重新钉回地板，于是身体高度不变、小腿向两边甩出去。实测：
    // 屈膝 1.55 rad 时 hips 仍在 0.988 m（与 rest 一样），"蹲"根本没蹲下去。
    float hipDrop = 25.0f;

    // 上身：蹲伏时躯干前倾，头抬起来看向前方（蹲着盯地面不自然）。
    float spineLean = 0.78f;  // rad，蹲下时躯干要明显前倾，否则读起来像往后坐
    float headLift  = 0.30f;  // rad，头反向仰起

    // 手臂：蹲伏时手臂自然垂在身前或抱住膝盖。这里选了"垂在身侧略向前"——
    // 抱膝需要手指骨，而参考资产没有。
    float armDown = 1.15f;    // rad，A-pose 下放的基准
    float armForward = 0.30f; // rad，手臂略向前
    float elbowBend = 0.65f;  // rad

    // 呼吸。周期独立于蹲姿本身，所以它是一个常驻的轻微运动。
    float breathSeconds = 3.2f;   // 一次呼吸
    float breathSpine   = 0.020f; // rad，胸腔起伏的幅度
    float breathSway    = 0.012f; // rad，极缓的左右重心摆动
};

/**
 * 原地蹲伏。循环（呼吸是周期的），`ContactPhase()` 为 -1：蹲着的时候没有
 * "落脚瞬间"这个概念，状态机不该为了等它而挂起。
 */
class ProceduralCrouch final : public AnimPoseSource {
public:
    ProceduralCrouch(const Skeleton& skeleton, const RigProfile& rig,
                     const std::vector<glm::mat4>& restWorld, const CrouchTuning& tuning);

    [[nodiscard]] const char* name() const noexcept override { return "crouch"; }
    [[nodiscard]] float Duration() const noexcept override { return tuning_.breathSeconds; }
    [[nodiscard]] bool Looping() const noexcept override { return true; }

    // 继承默认的 -1（"本动作没有落脚瞬间"）。显式写出来是为了让读代码的人
    // 知道这是决定，不是漏写。
    [[nodiscard]] float ContactPhase() const noexcept override { return -1.0f; }

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    [[nodiscard]] const CrouchTuning& tuning() const noexcept { return tuning_; }

private:
    const Skeleton&               skeleton_;
    const RigProfile&             rig_;
    const std::vector<glm::mat4>& restWorld_;
    CrouchTuning                  tuning_;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALCROUCH_H
