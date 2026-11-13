/**
 * @file AnimPoseSource.h
 * @brief Where a pose comes from - the one thing an animation layer has to abstract.
 *
 * 这个头文件只回答一个问题：**"在时刻 t，这具骨架该摆成什么样"**。至于那个姿态是
 * 三角函数算出来的（程序化步态，见 ProceduralWalk）、是从资产里采样出来的（关键帧
 * clip，尚未实现），还是干脆不动（静止），对上层没有区别——所以它是一个接口。
 *
 * ---- 为什么不直接把动作写成函数 ----
 *
 * 上一版的 `BuildWalkPose(sk, rig, restWorld, tune, phase, outPose)` 是个自由函数，
 * 能跑，但它把三件事糅在了一起：**姿态内容**（步态曲线）、**时间**（相位怎么推进）、
 * 以及**这段动作有多长**（调用方得自己知道 `2*pi` 是一个完整双步周期）。于是调用方
 * 想同时持有两段动作就必须自己复制一份时间逻辑，两份时间逻辑一旦不一致，"走→跳"
 * 的过渡就会在两个不同的时基之间做插值，表现为过渡期间动作速度突变。
 *
 * 接口化之后，时间归实现方管：`Duration()` 说这段动作多长，`Sample(t)` 说 t 时刻
 * 什么样，`Looping()` 说 t 超出 Duration 之后是回绕还是钳住。上层只认这三个问题。
 *
 * ---- 为什么输出是"局部矩阵数组"而不是别的 ----
 *
 * `outPose` 是每根骨头一个 local TRS 矩阵，长度 = `Skeleton::size()`。这正好是
 * `Skeleton::ComputeSkinning()` 的输入，也是 `gldx::Pose` 那组自由函数操作的同一
 * 种容器。**不用四元数数组**：Pose 层的全部工具（`RotateBone`/`ResetToRest`）都是
 * 在矩阵上工作的，换成四元数就得再造一套等价物，而两套表示之间的换算本身又是一处
 * 可能不一致的地方。
 *
 * 实现方必须遵守两条：
 *  1. **先写满整个 outPose**（`ResetToRest` 是最省事的做法），不要假设调用方预填了
 *     什么。`Sample` 可能被 `ResetToRest` 的产物之外的东西喂进来——别做增量累加，
 *     理由见 Pose.h 的"为什么要 reset 而非增量累加"。
 *  2. **`Sample` 必须是 t 的纯函数**：同一个 t 调两次必须给同一组矩阵，且不依赖
 *     调用顺序或之前的任何一次调用。`--freeze-at` 的可复现性直接建立在这一点上
 *     （时钟停住之后姿态仍然逐字节相同），而"往上一帧结果上继续乘"是最容易踩坏它的
 *     写法。
 */

#ifndef GLDX_ANIM_ANIMPOSESOURCE_H
#define GLDX_ANIM_ANIMPOSESOURCE_H

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

/**
 * 一段动作的姿态来源。
 *
 * 生命周期：实现方**不拥有** Skeleton / RigProfile / restWorld，它们由调用方持有且
 * 必须比 source 活得久。这里存指针而不是拷贝，是因为一个 23 骨骨架的 restWorld 缓存
 * 加分类结果几十 KB，而动画机每帧只是问一句"t 时刻什么样"。
 */
class AnimPoseSource {
public:
    virtual ~AnimPoseSource() = default;

    /**
     * 这段动作的名字，用于诊断与状态机转移（"walk" / "jump" / "idle"）。
     * 返回静态字符串，调用方不接管所有权。
     */
    [[nodiscard]] virtual const char* name() const noexcept = 0;

    /**
     * 一个完整周期的长度，秒。必须 > 0：状态机用它算相位、做过渡时长归一化，
     * 返回 0 会在除法和取模里变成 inf/NaN。
     */
    [[nodiscard]] virtual float Duration() const noexcept = 0;

    /** 超过 Duration 之后是否回绕到 0（行走为 true，跳跃为 false）。 */
    [[nodiscard]] virtual bool Looping() const noexcept = 0;

    /**
     * 把 t 折进这段动作的有效区间并写满 `outPose`（长度按 `Skeleton::size()`）。
     *
     * t 可以是任意实数（负的、超过 Duration 的）；实现方自己按 Looping() 决定回绕
     * 还是钳制。**不要让调用方先折好再传进来**——那样每个调用方都要复制一遍回绕
     * 逻辑，而跳跃的"钳在最后一帧"和行走的"取模"根本不是同一个操作。
     */
    virtual void Sample(float t, std::vector<glm::mat4>& outPose) const = 0;

    /**
     * 整个周期里脚步**最接近地面**的那一刻的归一化相位（0..1），或者 -1 表示
     * "这段动作没有落脚瞬间"（腾空、纯上肢体）。
     *
     * 为什么这条在接口上而不是留在实现里：状态机切动作时要挑时机。走路中途直接切
     * 跳跃，如果切在左腿悬空的那一刻，画面是"单脚站在地上起跳"；切在双支撑相
     * （两只脚都在地上）则自然得多。状态机不认识步态曲线，只认识这个数。
     */
    [[nodiscard]] virtual float ContactPhase() const noexcept { return -1.0f; }

    /**
     * 这段动作在 t 时刻是否**已经播完并停在最后一帧**。
     *
     * 只对 `Looping() == false` 有意义（循环动作永远返回 false）。默认实现就是按
     * Duration 判：非循环动作在 t >= Duration() 之后钳住末帧，所以那就是"播完了"。
     *
     * 为什么这条必须在接口上：`Sample` 只回答"t 时刻什么样"，而一个已经停在末帧的
     * 攻击动作与一个刚好播到末帧的攻击动作**姿态完全相同**，上层无法从姿态反推。
     * 没有这个数，状态机就只能靠猜——比如"非循环动作播了 2 秒大概够了吧"，而那个
     * 2 秒在换动作、换骨架之后就不对了。有了它，"挥砍播完自动回待机"才是上游能写的
     * 一条规则，而不是一段写死的计时。
     *
     * 实现方若在某段动作里改变了结束语义（例如收势后有一段静止的 follow-through，
     * 那段时间不该算"还需要等"），覆盖它即可。
     */
    [[nodiscard]] virtual bool FinishedAt(float t) const noexcept {
        if (Looping()) return false;
        return t >= Duration();
    }
};

} // namespace gldx

#endif // GLDX_ANIM_ANIMPOSESOURCE_H
