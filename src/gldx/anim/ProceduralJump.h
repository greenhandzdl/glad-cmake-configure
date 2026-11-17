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
    float crouchSeconds = 0.3f;   // 下蹲蓄力 + 蹬伸（同一段：先蹲后蹬）
    float airSeconds    = 0.55f;   // 离地到再次触地
    float landSeconds   = 0.26f;   // 落地屈膝缓冲

    // 腾空最高点，**米**。这是唯一一个世界单位（不是骨骼单位）的调参：它由调用方
    // 加进调色板（palette 的平移就是世界米），所以放在这里是为了"跳多高"只有一处。
    // 姿态本身（`Sample`）永远不写这个数——见下面那段"为什么弧线不在姿态里"。
    //
    // 0.55 m 其实已经是"立定跳"的量级，但**看起来只升了一点**：腾空段同时把腿收起来
    // （脚离地 0.73 m），而人眼量的是"脚离地多远"和"头顶到哪"，两者都被折腿吃掉
    // 一截。参考图（起跳悬空、双臂张开）里人的整体位移明显大于一个身高的一半，所以
    // 抬到 0.85 m；配合 `airArch` 的后仰与张开的双臂，可读性才够。
    float apexHeight    = 0.85f;

    // ---- 屈腿的符号，和 ProceduralCrouch 同一套 ----
    // 绕 side 的正向让朝下的骨头往后摆，所以**髋前屈是负、膝屈是正**。
    // `knee = 2 * hip` 这条比例把踝关节放在髋正下方（大腿小腿等长时两项相消），
    // 于是蹲下去时骨盆落在脚上方而不是脚后方 0.3 m——否则即使没跳，只是蹲一下，
    // 也会看起来像整个人朝前扑。
    float crouchHip  = 0.95f;   // rad，蓄力最深时的髋前屈
    float crouchKnee = 1.9f;   // rad，蓄力最深时的膝屈
    float landHip    = 0.70f;   // rad，落地最深时的髋前屈（比蓄力深一点）
    float landKnee   = 1.40f;   // rad，落地最深时的膝屈

    // ---- 腾空：双腿**向后折叠**、身体前倾 ----
    //
    // 参考图（空中那一帧）读下来是：人跳起来、**小腿向后折起**（脚跟朝臀部，大腿
    // 大致竖直略向后），上身略前倾、双臂向两侧张开。也就是说腿是"收起"的，不是
    // 笔直下垂、也不是向前平举。
    //
    // 这里已经翻过两次：
    //   第一次做成左右劈叉（一条前踢一条后踢）——那是跨栏；
    //   第二次做成"垂直起跳、双腿并拢伸直"——太素，读不出"跳起来"的感觉。
    // 判据是**两腿是否对称**（对称）+ **小腿是否离地收起**（收起）。
    //
    // 角度：髋略伸展（大腿在身体正下方偏后），膝深屈把小腿折起来，脚背绷直。
    float airHip     = 0.22f;  // rad，髋略伸（负 = 大腿在身体平面偏后）
    float airKnee    = 1.35f;   // rad，膝深屈：小腿向后折起（腾空的关键轮廓）
    float airAnkle   = 0.3f;   // rad，脚背绷直
    // 两腿略微分开，正侧面才不会两条腿叠成一条线；超过 ~0.1 就不再是"并拢"。
    // 绕 forward 的对称外展。`stanceSplit` 管蓄力/落地（两腿分到肩宽，重心才稳），
    // `airLegSplit` 管腾空（再张一点，轮廓更清楚）。
    float stanceSplit  = 0.16f;  // rad
    float airLegSplit  = 0.10f;  // rad

    // 两臂向两侧上方张开。**张开 = 绕 forward 轴转**（这具骨架的 T-pose 里上臂就
    // 指向 +side，所以"A-pose 基准"本身已经是斜下张开的，往回抬才是张开到斜上）。
    float airArmSpread = 1.3f;  // rad，相对 A-pose 回收的量
    float airArmRaise  = 0.75f;  // rad，额外上举（绕竖直轴）
    float airElbow     = 0.25f;  // rad，空中手臂几乎伸直
    // 躯干略后仰（参考图是挺胸的），头看前方。
    float airArch      = 0.1f;  // rad

    // 手臂：蓄力时向后摆、腾空时上举、落地时前伸缓冲。跳跃的手臂是"甩"出来的，
    // 它读起来比腿还重要——腿的幅度被裙子挡掉一半。
    float armBack = 0.50f;      // rad，蓄力后摆
    float armLand = 0.40f;      // rad，落地前伸
    float elbowBend = 0.45f;    // rad，常驻屈肘（两根直棍不像人）
};

/**
 * 一次跳跃。**不循环**：`Looping()` 为 false，所以 t 超过总时长后钳在最后一帧
 * （站直），而不是回绕到蓄力——回绕会让角色无限连跳，那不是"一次跳跃"。
 */
/**
 * ---- 为什么腾空弧线不在姿态里 ----
 *
 * `Sample()` 输出的是**骨骼局部矩阵**，它的平移量在骨骼空间（这具资产 x0.01），
 * 而"跳多高"是世界米。两者之间隔着 `RiggedModel::Mat4()` 的 unitScale，姿态层拿不到
 * 也不该拿到那个数。更要紧的是：地面解算（`GroundContact`）已经把骨盆高度交给了
 * 解算器——姿态层再写一个"上升多少"，就会和解算打架，而打架的结果是脚陷进地面或
 * 悬空，并且没有任何报错。
 *
 * 所以分工是：姿态只管关节（腿怎么折），高度归调用方（`AirborneRiseAt` + 站立高度）。
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
     *
     * ⚠️ **它只回答"要不要解算"，不回答"人在多高"**。腾空期间真正的抬升是这个类
     * 的另一半 `AirborneRiseAt(t)`，两半都要消费：只关解算而不加弧线，角色会在空中
     * 保留站立高度、双腿收着——"被一根看不见的绳子吊着"正是那个样子，而这套接口
     * 之前**没有任何调用方**读过 `AirborneRiseAt`（全仓搜索为零），所以跳跃在空中
     * 既不上升也不下降。
     */
    [[nodiscard]] bool GroundedAt(float t) const noexcept;

    /**
     * 时刻 t 相对"站立高度"的抬升（米）。腾空段是抛物线，蓄力/落地段是 0。
     *
     * 调用方的用法是把 `站立高度 + rise` 当作这一刻的整具角色高度：站立高度是
     * 直腿站姿的鞋底高度（demo 用 `GroundContact::Solve(rest)` 量一次），而本动作
     * 在起飞与触地两帧的姿态恰好就是直腿站姿，所以两支接得上、没有台阶。
     */
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
