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
    //
    // 这具骨架的手臂只有 4 根骨（锁骨/上臂/前臂/手），所以"挥剑"只能靠两连杆的
    // 胳膊演。关键在于**顺序**与**轴**：
    //
    //   1. 先把手臂从 T-pose 放下来到 A-pose（armDown）。这是常驻基准。
    //   2. 抬（绕 FORWARD 轴）：把上臂从斜下抬到侧上方。**先做这一步**——手臂还
    //      垂着的时候绕竖直轴转，等于让它绕自身长轴自旋，看起来完全没动。
    //   3. 扫（绕 UP 轴）：在抬起来的高度上做横向圆弧，这才是"挥"。
    //   4. 屈肘/扣腕：把"剑"收到肩后（蓄力）或甩到身前（挥出）。
    //
    // 之前的版本把 2/3 写反了（先绕 up 扫、再抬很少），实测手腕相对肩的矢量在
    // 整段里只动了 0.3 m（trace 的 handLen 列），画面就是一具站着的人。
    const float armRaised = (drive < 0.0f) ? tune.windupArmUp * (-drive)
                                           : tune.swingArmUp * drive;
    const float armSweep  = (drive < 0.0f) ? tune.windupArmBack * (-drive)
                                           : tune.swingArmBack * drive;
    const float elbow     = (drive < 0.0f) ? tune.windupElbow * (-drive)
                                           : tune.swingElbow * drive;
    const float wrist     = (drive < 0.0f) ? 0.0f : tune.swingWrist * drive;

    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armDown);
    // ---- 抬臂绕 SIDE 转，不是绕 UP ----
    //
    // 这段原来绕 `up`，理由是"支架是 T-pose，上臂静止方向就是 +side，绕竖直轴才
    // 抬得起来"。那条前提对参考资产成立，对本资产**不成立**：它的 bind 是 A-pose
    // （上臂 up -1.85 / side +1.87，已在水平下 44.8°），而 `armDown = 0.790` 正好
    // 把剩下的 45.2° 补满，手臂垂到**完全垂直**。垂直的手臂绕 `up` 转 = 绕自身长轴
    // 自旋，侧向投影为 0——实测截图四帧一模一样，`armRaised` 写了 2.3 rad 也没用。
    //
    // 轴的选择是几何决定的，不是约定：`armDown` 之后上臂已经**垂直向下**，要把它
    // 抬向 +side，旋转轴必须是 `v0 x v1 = (0,-1,0) x (1,0,0) = (0,0,1)`，也就是
    // FORWARD。绕 `side` 会与手臂长轴平行，等于自旋（改错过一次，画面纹丝不动）；
    // 绕 `up` 只对 T-pose 成立。
    //
    // 横扫（前后）才是绕 `side`：手臂抬平之后，绕侧轴转就是把它从身前扫到身后。
    rotate(rig_.armUp[kRigRight], rig_.forward, -armRaised);
    rotate(rig_.armUp[kRigRight], rig_.side, -armSweep * 0.5f);
    rotate(rig_.armLow[kRigRight], rig_.side, -elbow);
    rotate(rig_.hand[kRigRight], rig_.side, wrist);

    // ---- 左臂（配平）----
    // 挥砍时左臂朝相反方向展开，这是让动作读起来"用了全身"的关键。
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armDown);
    rotate(rig_.armUp[kRigLeft], rig_.forward, tune.offArmLift * std::abs(drive));
    rotate(rig_.armUp[kRigLeft], rig_.up, tune.offArmSwing * drive);
    rotate(rig_.armLow[kRigLeft], rig_.side, -(0.55f + 0.30f * std::abs(drive)));
    rotate(rig_.armLow[kRigLeft], rig_.up, 0.12f);
    rotate(rig_.hand[kRigLeft], rig_.side, -0.15f);
}

} // namespace gldx
