/**
 * @file ProceduralAttack.h
 * @brief 程序化挥砍：蓄力 → 挥出 → 收势，一条右臂主导的三段曲线。
 *
 * ---- 为什么这个动作程序化做得到，而"真正的战斗动画"做不到 ----
 *
 * 挥砍能被三角函数撑住，是因为它的结构极简：**一条弧线，两端速度为零，中段最快**。
 * 这正好是 smoothstep 的形状。人眼对挥砍的全部判据就三条：
 *
 *   1. **反方向蓄力**（先往后拉），否则挥击没有起点；
 *   2. **中段最快**，两端慢——这是"有重量"的唯一来源；
 *   3. **收势**不能省，停在挥出末端看起来像卡住了。
 *
 * 至于剑尖轨迹是否真的走一条漂亮的弧、肩胛怎么参与、脚步怎么配合，那是手 K
 * 才做得好的部分。所以这里的诚实定位是：**一个能读作"攻击"的占位动作**，等到
 * 关键帧 clip 读取接进来，它应该被真实片段替换掉，而不是继续加参数。
 *
 * ---- 为什么不分左右手 ----
 *
 * `side` 参数选了右臂（多数人右手持械）。做成"任意手"的代价是每条曲线都要
 * 乘一个 ±1 的符号，而符号错误的表现是"攻击时左手往前甩"——一个不该靠试出来的
 * 一类错误。真要左手，复制一份 tuning 改符号比加参数安全。
 *
 * ---- 与地面接触的关系 ----
 *
 * 攻击全程双脚着地，所以 `GroundedAt` 恒为 true，调用方照常做地面解算。
 * 挥砍时重心会前后移动，但那由骨盆**姿态**承担（髋的旋转），不是靠抬高骨盆。
 */

#ifndef GLDX_ANIM_PROCEDURALATTACK_H
#define GLDX_ANIM_PROCEDURALATTACK_H

#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

struct AttackTuning {
    float windupSeconds = 0.18f;   // 蓄力（往后拉）
    float swingSeconds  = 0.14f;   // 挥出（这一段必须短，快才有力量感）
    float recoverSeconds = 0.30f;  // 收势（回到站姿）

    // 蓄力段：右臂向后上方拉，躯干向右后转。
    float windupArmBack  = 1.05f;  // rad，上臂后摆
    float windupArmUp    = 0.55f;  // rad，上臂抬起（举过头侧）
    float windupElbow    = 1.35f;  // rad，屈肘把"武器"收到肩后
    float windupTorsoYaw = 0.42f;  // rad，躯干向右转（蓄势）
    float windupLean     = -0.10f; // rad，略微后仰

    // 挥出段：右臂向前下方划过，躯干向左转回来并前倾。
    float swingArmBack   = -0.85f; // rad，上臂前摆（负 = 朝前）
    float swingElbow     = 0.20f;  // rad，挥出时手臂几乎伸直
    float swingTorsoYaw  = -0.50f; // rad，躯干向左转
    float swingLean      = 0.28f;  // rad，前倾（把体重送进去）

    // 左臂的反向配平：挥砍时左臂向后摆以平衡，这是让动作读起来"用了全身"的关键。
    float offArmSwing    = 0.55f;  // rad

    // 腿部：右脚在后蹬地，左脚在前承重。站立基础是错步（前后开立）。
    float stanceStagger  = 0.22f;  // rad，两条腿的前后差
    float stanceKnee     = 0.18f;  // rad，基础屈膝

    // 手臂下放的基准（避免 T-pose 直接挥）。
    float armDown = 1.02f;
};

/**
 * 一次挥砍。**不循环**：播完停在收势结束的站姿，不回绕——回绕会让角色无限挥砍，
 * 而"一次攻击"是有起止的事件。
 *
 * ---- 播完之后停在哪：不是 rest pose ----
 *
 * 最后一帧不等于 `ResetToRest()` 的结果，因为 `armDown` 是**常驻**的：这具骨架的
 * bind pose 是 T-pose，而挥砍必须发生在一条自然下垂的手臂上，否则"挥"读起来像
 * 把整条手臂从肩膀横着扫地。所以末帧是"手臂下放的站立姿态"。
 *
 * 这不是近似或缺陷，但调用方必须知道：**从攻击切回行走时会有一次过渡**（因为
 * 行走同样带 armDown，两者在这一点上一致，所以过渡很短、看不出），而从攻击切到
 * 一个真正静止的 rest pose 时，手臂会突然横回去——那看起来像 bug，实际是两个
 * 动作对"站立"的定义不同。想要干净的收尾，就让收势结束后切到另一个动作，而不是
 * 停在 rest。
 */
class ProceduralAttack final : public AnimPoseSource {
public:
    ProceduralAttack(const Skeleton& skeleton, const RigProfile& rig,
                     const std::vector<glm::mat4>& restWorld, const AttackTuning& tuning);

    [[nodiscard]] const char* name() const noexcept override { return "attack"; }
    [[nodiscard]] float Duration() const noexcept override { return duration_; }
    [[nodiscard]] bool Looping() const noexcept override { return false; }

    /**
     * 命中判定时刻的归一化相位：挥出段的**中点**。调用方可以拿它决定什么时候
     * 造成伤害/播放音效——这与渲染无关，但放在实现里是因为只有实现知道
     * "挥出"和"蓄力"的分界线在哪。
     */
    [[nodiscard]] float ContactPhase() const noexcept override;

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    /**
     * 时刻 t 是否已经越过命中点。比 `ContactPhase` 更好用的形式：调用方每帧
     * 问一次，用来触发一次性的副作用（伤害、音效），不必自己比较相位。
     */
    [[nodiscard]] bool HitReachedAt(float t) const noexcept;

    [[nodiscard]] const AttackTuning& tuning() const noexcept { return tuning_; }

private:
    const Skeleton&               skeleton_;
    const RigProfile&             rig_;
    const std::vector<glm::mat4>& restWorld_;
    AttackTuning                  tuning_;
    float                         duration_ = 1.0f;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALATTACK_H
