module;

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿；不文本 include 自己的头（会报 redefinition）。

namespace gldx {
namespace {

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float Ease01(float x) {
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
float Segment01(float x, float a, float b) {
    return Clamp01((x - a) / std::max(b - a, 1e-5f));
}

} // namespace

ProceduralAttack::ProceduralAttack(const Skeleton& skeleton, const RigProfile& rig,
                                   const std::vector<glm::mat4>& restWorld,
                                   const AttackTuning& tuning)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning) {
    // 三段各夹正下限：0 秒会让下面每处除法分母归零，而 NaN 进姿态矩阵的表现是
    // "角色整个消失"。
    duration_ = std::max(tuning_.windupSeconds, 1e-3f) +
                std::max(tuning_.swingSeconds, 1e-3f) +
                std::max(tuning_.recoverSeconds, 1e-3f);
}

float ProceduralAttack::ContactPhase() const noexcept {
    const float w = std::max(tuning_.windupSeconds, 1e-3f);
    const float s = std::max(tuning_.swingSeconds, 1e-3f);
    // 挥出段的中点：这是手最快、也最可能是"碰到东西"的那一刻。
    return (w + s * 0.5f) / duration_;
}

bool ProceduralAttack::HitReachedAt(float t) const noexcept {
    const float w = std::max(tuning_.windupSeconds, 1e-3f);
    const float s = std::max(tuning_.swingSeconds, 1e-3f);
    return t >= (w + s * 0.5f);
}

void ProceduralAttack::Sample(float t, std::vector<glm::mat4>& outPose) const {
    ResetToRest(skeleton_, outPose);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const AttackTuning& tune = tuning_;
    const float w = std::max(tune.windupSeconds, 1e-3f);
    const float s = std::max(tune.swingSeconds, 1e-3f);
    const float r = std::max(tune.recoverSeconds, 1e-3f);
    const float total = w + s + r;

    // 钳制而不是回绕：不循环的动作在 t 超过时长后停在最后一帧（站直）。
    const float tt = std::clamp(t, 0.0f, total);

    // 三段的进度，各自只在所在段里是 0..1。
    const float windU = Segment01(tt, 0.0f, w);
    const float swingU = Segment01(tt, w, w + s);
    const float recU = Segment01(tt, w + s, total);

    // ---- 一个"挥砍量"标量 ----
    // 与其给每个关节分别写三段曲线（那样三处容易不同步），不如把它压成一个
    // 主控量：-1 = 完全蓄力到位（向后），+1 = 完全挥出（向前），0 = 站立。
    //
    // 蓄力段：0 → -1，缓入（先慢后快，像拉开了弓）
    // 挥出段：-1 → +1，且中段最快 —— 这是 smoothstep 的形状，也是"有重量"的来源
    // 收势段：+1 → 0，缓慢归位
    float drive = 0.0f;
    if (tt < w) {
        drive = -Ease01(windU);
    } else if (tt < w + s) {
        drive = -1.0f + 2.0f * Ease01(swingU);
    } else {
        drive = 1.0f - Ease01(recU);
    }

    // 前倾/后仰：蓄力时略微后仰（-），挥出时前倾（+）。
    const float lean = (drive < 0.0f) ? tune.windupLean * (-drive) : tune.swingLean * drive;
    // 躯干扭转：蓄力向右（+），挥出向左（-）。用 drive 直接映射，两者天然同步。
    const float torsoYaw = (drive < 0.0f) ? tune.windupTorsoYaw * (-drive)
                                          : tune.swingTorsoYaw * drive;

    // ---- 躯干 ----
    rotate(rig_.spine, rig_.side, lean * 0.6f);
    rotate(rig_.chest, rig_.side, lean * 0.4f);
    rotate(rig_.spine, rig_.up, torsoYaw * 0.45f);
    rotate(rig_.chest, rig_.up, torsoYaw * 0.55f);
    // 头保持看向正前方：躯干转了多少，头就转回来多少。
    rotate(rig_.head, rig_.up, -torsoYaw * 0.85f);

    // ---- 站立基础：错步 ----
    // 前后开立的站姿。全部动作都叠在这个基础之上，所以攻击看起来是"站稳了挥"
    // 而不是"双脚并拢原地挥手"。
    for (int side = 0; side < kRigSides; ++side) {
        const float stagger = (side == 0) ? -tune.stanceStagger : tune.stanceStagger;
        rotate(rig_.legUp[side], rig_.side, stagger);
        rotate(rig_.legLow[side], rig_.side, tune.stanceKnee);
        rotate(rig_.foot[side], rig_.side, -tune.stanceKnee * 0.6f);
    }

    // ---- 右臂（持械手）----
    // 先下放到 A-pose 基准，这样"挥"才是前后方向而不是绕自身长轴自转。
    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armDown);

    // 上臂：蓄力时向后上方拉，挥出时向前下方划过。
    // drive < 0 取 windup 的角，drive > 0 取 swing 的角，两者在 drive=0 处自然
    // 都退回到 A-pose 基准，所以段与段之间不会跳。
    const float armBack = (drive < 0.0f) ? tune.windupArmBack * (-drive)
                                         : tune.swingArmBack * drive;
    const float armUp = (drive < 0.0f) ? tune.windupArmUp * (-drive) : 0.0f;
    const float elbow = (drive < 0.0f) ? tune.windupElbow * (-drive)
                                       : tune.swingElbow * drive;

    // 绕侧向轴的**正**向 = 向后，所以 armBack 直接用（正值=向后拉）。
    rotate(rig_.armUp[kRigRight], rig_.side, armBack);
    // 抬起到头侧。绕前向轴的负向是"往外/往上"抬起。
    rotate(rig_.armUp[kRigRight], rig_.forward, -armUp);
    rotate(rig_.armLow[kRigRight], rig_.side, -elbow);
    // 挥出时手腕略压，让"武器"跟着走。
    rotate(rig_.hand[kRigRight], rig_.side, -elbow * 0.35f);

    // ---- 左臂（配平）----
    // 挥砍时左臂朝相反方向摆，这是让动作读起来"用了全身"的关键；只在右臂动
    // 的话看起来像甩了一下手。
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armDown);
    rotate(rig_.armUp[kRigLeft], rig_.side, -tune.offArmSwing * drive);
    rotate(rig_.armLow[kRigLeft], rig_.side, -(0.55f + 0.35f * std::abs(drive)));
    rotate(rig_.armLow[kRigLeft], rig_.up, 0.12f);
    rotate(rig_.hand[kRigLeft], rig_.side, -0.15f);
}

} // namespace gldx
