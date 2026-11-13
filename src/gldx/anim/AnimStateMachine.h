/**
 * @file AnimStateMachine.h
 * @brief 动画机：在若干段动作之间切换，并在切换时做混合。
 *
 * ---- 它到底解决什么问题 ----
 *
 * 有了 `AnimPoseSource` 之后，"某段动作 t 时刻什么样"已经能问了。但真实角色还要回答
 * 另外三件事，而这三件事**不属于任何一段动作**：
 *
 *  1. 现在该播哪一段（状态）；
 *  2. 从上一段换到这一段时，中间那 0.15 秒怎么过去（过渡）；
 *  3. 什么时候切换才不难看（时机）。
 *
 * 第三点尤其容易被忽略：走路中途直接切跳跃，如果切在左腿悬空那一刻，画面是
 * "单脚站在地上起跳"。`AnimPoseSource::ContactPhase()` 就是为这件事存在的。
 *
 * ---- 过渡为什么是"权重混合矩阵"而不是"混合参数" ----
 *
 * 混合两份姿态最直接的做法是逐元素插值矩阵：`out[i] = lerp(a[i], b[i], w)`。它对
 * 平移和缩放是对的，对旋转**在小角度下看着也像对的**，但它不是旋转空间的插值——
 * 两份姿态差 180° 时线性插值会把骨骼压扁（矩阵的旋转部分不再正交，角色在中途
 * "缩成一条线"）。正确做法是把旋转转成四元数再 slerp。
 *
 * 这里选逐元素 lerp 而不是四元数，是一个**有意的取舍**：现有动作全部来自同一个
 * `RotateBone` 叠出来的小角度旋转（最大不过 1 叫 rad），两份姿态之间的差异很小，
 * lerp 的不正交误差在视觉上不可见；而引入四元数意味着要么给 Pose 层再造一套
 * 四元数工具，要么在每帧对 23 根骨头做 矩阵→四元数→slerp→矩阵 的往返，那个往返
 * 本身又是一处可能不一致的地方。
 *
 * **代价是真实的**：将来若接入手工关键帧动作（可能出现大角度差），这套 lerp 会在
 * 过渡途中把肢体压扁。届时的修法是把这个函数换成四元数 slerp，而调用方不用改——
 * 混合被刻意关在这一个类里，就是为了让那次替换是局部的。
 *
 * ---- 为什么不拥有动作 ----
 *
 * 状态机只**引用** `AnimPoseSource`（存 `const AnimPoseSource*`），不复制、不拥有。
 * 一段行走的 tuning 属于调用方（它要跟着资产重调），状态机不该替它持有。代价是
 * 调用方必须保证 source 活得比状态机久，且**不要在状态机还引用着它的时候销毁**。
 */

#ifndef GLDX_ANIM_ANIMSTATEMACHINE_H
#define GLDX_ANIM_ANIMSTATEMACHINE_H

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace gldx {

class AnimPoseSource;

/**
 * 一次状态转移的规格。
 */
struct AnimTransition {
    /** 过渡时长，秒。0 表示硬切（当帧就变），适合"必须立刻响应"的场合。 */
    float seconds = 0.15f;

    /**
     * 是否等到**当前动作的落脚时刻**再开始切。
     *
     * 行走这种循环动作置 true：切在双支撑相（两脚都着地）看起来像在换步；切在
     * 单腿悬空时则像踉跄。跳跃这种不循环、或必须立刻响应的动作置 false。
     *
     * 注意"等"的语义：置 true 时请求**不会丢失**，会一直挂到合适的相位再执行，
     * 期间继续播旧动作。这就是为什么 `Request` 之后 `state()` 可能还是旧值。
     */
    bool atContact = false;

    /** 等相位时的超时，秒。超过就直接切，避免永远等不到（例如目标动作没有接触相位）。 */
    float contactTimeout = 0.6f;
};

/**
 * 一台动画机。每帧：喂 dt → 按需 `Request` → `Evaluate` 拿到当前姿态。
 *
 * 典型用法（顺序很重要）：
 *
 *     machine.Request(&jump);                       // 想跳
 *     machine.Update(dt);                           // 推进时钟、执行转移
 *     machine.Evaluate(machine.Current(), pose);    // 拿到混合后的姿态
 *
 * `Update` 必须在 `Evaluate` 之前：转移是在 `Update` 里才真正开始生效的，先
 * `Evaluate` 会拿到转移前那一刻的姿态（表现为"按了键没反应，下一帧才动"）。
 */
class AnimStateMachine {
public:
    AnimStateMachine() = default;

    /**
     * 起始状态（硬切，无过渡）。必须在使用 `Update` 之前调用一次，否则
     * `Current()` 返回 nullptr、`Evaluate` 不写任何东西。
     */
    void Set(const AnimPoseSource* source);

    /**
     * 请求切到 `source`。`source` 为 nullptr 时忽略（表示"没有动作可播"）。
     *
     * 重复请求同一个 source 是无害的 no-op：正在播 walk 又请求 walk 不会重启动画，
     * 否则每帧调一次的调用方会把相位反复清零，角色原地抽搐。
     */
    void Request(const AnimPoseSource* source, const AnimTransition& transition = {});

    /** 推进 dt 秒：累加旧/新动作各自的时间，执行挂起的转移。 */
    void Update(float dt);

    /**
     * 把当前姿态写进 `outPose`（长度按骨架，会被 Resize 到当前动作的骨头数）。
     * 转移中则按权重混合新旧两份。
     *
     * 慢动作/快进靠缩放喂给 `Update` 的 dt，而不是这里的参数：那样时间推进只有
     * 一处，不会出现"内部时钟按实时走、采样按慢速取"这种两份时基。
     *
     * 返回 false 表示还没有当前动作（没 `Set` 过），此时 `outPose` 不被写入。
     */
    bool Evaluate(std::vector<glm::mat4>& outPose) const;

    /** 当前正在播的动作；未 `Set` 过则为 nullptr。 */
    [[nodiscard]] const AnimPoseSource* Current() const noexcept { return current_; }
    /** 正在过渡进来的动作；不在过渡中则为 nullptr。 */
    [[nodiscard]] const AnimPoseSource* Pending() const noexcept { return pending_; }
    /** 当前动作内部的时刻（秒），可直接喂给 `Current()->Sample()`。 */
    [[nodiscard]] float CurrentTime() const noexcept { return currentTime_; }
    /** 过渡进度 0..1；不在过渡中则为 1（表示"已经是新动作"）。 */
    [[nodiscard]] float BlendWeight() const noexcept { return blendWeight_; }
    [[nodiscard]] bool Transitioning() const noexcept { return pending_ != nullptr; }

    /**
     * 当前动作是否已经播完（非循环动作停在末帧）。供调用方写"攻击播完回待机"
     * 这类回落规则——状态机自己**不**做这个决定：往哪回落是应用的事（战斗时
     * 回战斗待机、探索时回普通待机），状态机只负责如实报告。
     */
    [[nodiscard]] bool Finished() const noexcept {
        return current_ != nullptr && current_->FinishedAt(currentTime_);
    }

private:
    /** 把 t 按 source 的循环性折进有效区间。 */
    static float Fold(const AnimPoseSource& source, float t);

    const AnimPoseSource* current_ = nullptr;
    const AnimPoseSource* pending_ = nullptr;

    float currentTime_ = 0.0f;   // current_ 内部的时刻
    float pendingTime_ = 0.0f;   // pending_ 内部（只从开始过渡那一刻算起）
    float blendWeight_ = 1.0f;   // 0 = 全是旧动作, 1 = 全是新动作
    float blendDuration_ = 0.0f; // 本次过渡的总时长；0 表示硬切已生效

    // 挂起等待接触相位的请求
    const AnimPoseSource* requested_ = nullptr;
    AnimTransition        requestedSpec_{};
    float                 requestedWait_ = 0.0f;   // 已经等了多久

    // 混合用的两份临时姿态。放成员里而不是每帧现分配：这是每帧都走的路径，
    // 一个 23 骨骨架两份 vector 是 ~1.5 KB 的分配 + 释放，白扔掉。
    mutable std::vector<glm::mat4> scratchA_;
    mutable std::vector<glm::mat4> scratchB_;
};

} // namespace gldx

#endif // GLDX_ANIM_ANIMSTATEMACHINE_H
