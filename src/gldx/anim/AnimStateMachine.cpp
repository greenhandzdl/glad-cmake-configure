module;

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿；不文本 include 自己的头（会报 redefinition）。

namespace gldx {

float AnimStateMachine::Fold(const AnimPoseSource& source, float t) {
    const float duration = source.Duration();
    // Duration() 契约上 > 0，但它是虚函数：一个写错的实现返回 0 会让下面的取模
    // 变成 fmod(x, 0) = NaN，而 NaN 传染进姿态矩阵的表现是"角色整个消失"。
    // 在这里兜一层，代价是一个比较。
    if (!(duration > 0.0f)) return 0.0f;

    if (source.Looping()) {
        // 取模，且保证结果非负（t 可能为负，例如过渡把相位往回拉）。
        float wrapped = std::fmod(t, duration);
        if (wrapped < 0.0f) wrapped += duration;
        return wrapped;
    }
    // 不循环：钳在两端。跳跃播完就停在最后一帧，而不是回绕到蓄力段。
    return std::clamp(t, 0.0f, duration);
}

void AnimStateMachine::Set(const AnimPoseSource* source) {
    current_ = source;
    pending_ = nullptr;
    requested_ = nullptr;
    currentTime_ = 0.0f;
    pendingTime_ = 0.0f;
    blendWeight_ = 1.0f;
    blendDuration_ = 0.0f;
    requestedWait_ = 0.0f;
}

void AnimStateMachine::Request(const AnimPoseSource* source, const AnimTransition& transition) {
    if (source == nullptr) return;

    // 已经在播它（或是正在过渡进来的目标）就是 no-op。这一条是必要的：调用方通常
    // 每帧根据输入调一次 Request，若每帧都重启相位，角色会原地抽搐而不是走路。
    if (source == current_ || source == pending_) return;

    if (transition.atContact && current_ != nullptr && current_->ContactPhase() >= 0.0f) {
        // 挂起等待落脚相位。请求不会丢，期间继续播旧动作。
        requested_ = source;
        requestedSpec_ = transition;
        requestedWait_ = 0.0f;
        return;
    }

    requested_ = nullptr;
    pending_ = source;
    pendingTime_ = 0.0f;
    blendWeight_ = 0.0f;
    blendDuration_ = std::max(transition.seconds, 0.0f);
}

void AnimStateMachine::Update(float dt) {
    // dt 为负（时钟回拨、或调用方算错）时钳到 0：让时间倒退会让 fmod 的结果跳变，
    // 表现为姿态瞬移。宁可这一帧不动。
    if (!(dt > 0.0f)) dt = 0.0f;

    if (current_ != nullptr) {
        currentTime_ = Fold(*current_, currentTime_ + dt);
    }

    // 挂起的"等相位"请求：每帧检查当前动作是不是到了落脚点。
    if (requested_ != nullptr) {
        requestedWait_ += dt;
        bool fire = false;

        if (current_ == nullptr) {
            fire = true;
        } else {
            const float contact = current_->ContactPhase();
            if (contact >= 0.0f) {
                // 判断"这一帧跨过了落脚相位"。用跨越判定而不是"等于"：帧长通常
                // 远大于一帧的相位增量，等值判定几乎永远不成立。
                const float duration = current_->Duration();
                if (duration > 0.0f) {
                    const float phase = currentTime_ / duration;      // 0..1
                    const float prevPhase =
                        std::fmod(std::max(currentTime_ - dt, 0.0f), duration) / duration;
                    // 相位回绕（prev > phase）时，区间跨过 0 也要算命中。
                    const bool wrapped = prevPhase > phase;
                    fire = (prevPhase <= contact && contact <= phase) ||
                           (wrapped && contact >= prevPhase);
                }
            }
            // 超时兜底：目标动作可能根本没有接触相位，或者当前动作的时长异常，
            // 不该让请求永远挂着（那会让按键"没反应"）。
            if (!fire && requestedWait_ >= requestedSpec_.contactTimeout) fire = true;
        }

        if (fire) {
            pending_ = requested_;
            requested_ = nullptr;
            pendingTime_ = 0.0f;
            blendWeight_ = 0.0f;
            blendDuration_ = std::max(requestedSpec_.seconds, 0.0f);
        }
    }

    // 推进过渡。
    if (pending_ != nullptr) {
        pendingTime_ = Fold(*pending_, pendingTime_ + dt);
        if (blendDuration_ > 0.0f) {
            blendWeight_ = std::min(1.0f, blendWeight_ + dt / blendDuration_);
        } else {
            blendWeight_ = 1.0f;   // 硬切：立刻完成
        }
        if (blendWeight_ >= 1.0f) {
            // 过渡结束：新动作成为当前动作，时间沿用它在过渡期间已走到的位置
            // （而不是清零），否则过渡结束那一帧动作会倒退。
            current_ = pending_;
            currentTime_ = pendingTime_;
            pending_ = nullptr;
            blendWeight_ = 1.0f;
        }
    }
}

bool AnimStateMachine::Evaluate(std::vector<glm::mat4>& outPose) const {
    if (current_ == nullptr) return false;

    if (pending_ == nullptr || blendWeight_ >= 1.0f) {
        // 没有过渡：直接采样当前动作。
        current_->Sample(currentTime_, outPose);
        return true;
    }

    // 过渡中：两份都采出来再按权重插值。
    current_->Sample(currentTime_, scratchA_);
    pending_->Sample(pendingTime_, scratchB_);

    // 两份姿态的骨头数必须一致（它们属于同一具骨架）。真的不一致时按较短的来，
    // 剩下的部分留旧动作——比读越界好，也比整帧不动更容易被发现。
    const std::size_t count = std::min(scratchA_.size(), scratchB_.size());
    outPose.resize(std::max(scratchA_.size(), scratchB_.size()));
    const float w = std::clamp(blendWeight_, 0.0f, 1.0f);

    for (std::size_t i = 0; i < count; ++i) {
        outPose[i] = scratchA_[i] * (1.0f - w) + scratchB_[i] * w;
    }
    for (std::size_t i = count; i < outPose.size(); ++i) {
        outPose[i] = (i < scratchA_.size()) ? scratchA_[i] : scratchB_[i];
    }
    return true;
}

} // namespace gldx
