module;

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿，不文本 include 自己的头（否则报 redefinition）。

namespace gldx {
namespace {

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float Ease01(float x) {
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
// 把 x 从 [a,b] 映射到 0..1，区间外钳住。
float Segment01(float x, float a, float b) {
    return Clamp01((x - a) / std::max(b - a, 1e-5f));
}

} // namespace

ProceduralJump::ProceduralJump(const Skeleton& skeleton, const RigProfile& rig,
                               const std::vector<glm::mat4>& restWorld,
                               const JumpTuning& tuning)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning) {
    // 三段各夹一个正下限：0 秒的段会让下面每一处除法的分母归零，而 NaN 传染进
    // 姿态矩阵的表现是"角色整个消失"，比崩溃更难查。
    const float c = std::max(tuning_.crouchSeconds, 1e-3f);
    const float a = std::max(tuning_.airSeconds, 1e-3f);
    const float l = std::max(tuning_.landSeconds, 1e-3f);
    duration_ = c + a + l;
}

float ProceduralJump::ContactPhase() const noexcept {
    // 落地开始的归一化相位。状态机用它决定"什么时候切回行走最自然"。
    const float c = std::max(tuning_.crouchSeconds, 1e-3f);
    const float a = std::max(tuning_.airSeconds, 1e-3f);
    return (c + a) / duration_;
}

bool ProceduralJump::GroundedAt(float t) const noexcept {
    const float c = std::max(tuning_.crouchSeconds, 1e-3f);
    const float a = std::max(tuning_.airSeconds, 1e-3f);
    // 钳住 t：超过总时长之后角色已经站定，那就是踩在地上的。
    const float tt = std::clamp(t, 0.0f, duration_);
    return tt < c || tt >= (c + a);
}

float ProceduralJump::AirborneRiseAt(float t) const noexcept {
    const float c = std::max(tuning_.crouchSeconds, 1e-3f);
    const float a = std::max(tuning_.airSeconds, 1e-3f);
    if (t < c || t >= (c + a)) return 0.0f;

    // 真抛物线：u 在 [0,1] 是腾空进度，4u(1-u) 在 u=0 和 u=1 处为 0、u=0.5 处为 1。
    // 这个形状两端陡、顶点平，正是抛体；正弦恰好相反（两端平、中间陡），人眼对
    // 这两者的差异很敏感。
    const float u = (t - c) / a;
    return tuning_.apexHeight * 4.0f * u * (1.0f - u);
}

void ProceduralJump::Sample(float t, std::vector<glm::mat4>& outPose) const {
    // 每次都从 rest 开始，不做增量（理由见 Pose.h）。
    ResetToRest(skeleton_, outPose);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const JumpTuning& tune = tuning_;
    const float c = std::max(tune.crouchSeconds, 1e-3f);
    const float a = std::max(tune.airSeconds, 1e-3f);
    const float l = std::max(tune.landSeconds, 1e-3f);
    // 钳住而不是回绕：不循环的动作在 t 超过时长后停在最后一帧（站直）。
    const float tt = std::clamp(t, 0.0f, duration_);

    // 三段各自的进度，只有所在的那一段是 0..1，其余段钳在 0 或 1。
    const float crouchU = Segment01(tt, 0.0f, c);
    const float airU    = Segment01(tt, c, c + a);
    const float landU   = Segment01(tt, c + a, duration_);

    // ---- 腿部 ----
    // 蓄力：从站直弯到最深（crouchU 的缓入），但落地段结束后要回到站直，所以
    // 用一个"深度"标量而不是直接乘角度：越到后面越浅。
    //
    // 关键细节：蓄力段结束与腾空段开始必须**连续**。蓄力末 knee = crouchKnee，
    // 腾空初立刻收腿到 airTuckKnee——若两个值差得远，离地那一帧膝盖会瞬移。
    // 所以腾空的起始姿态取蓄力末值，再往收腿姿态过渡。
    float kneeDepth = 0.0f;
    float hipDepth  = 0.0f;
    float ankle     = 0.0f;
    float armRaise  = 0.0f;

    if (tt < c) {
        // 蓄力：缓入到最深。
        const float k = Ease01(crouchU);
        kneeDepth = tune.crouchKnee * k;
        hipDepth  = tune.crouchHip * k;
        ankle     = tune.crouchAnkle * k;
        // 蓄力时手臂向后下方摆（为起跳蓄势），用负的 armRaise 表示后摆。
        armRaise  = -0.35f * k;
    } else if (tt < c + a) {
        // 腾空：前 35% 把腿蹬直（离地那一下是伸直的），之后收膝准备落地。
        const float extend = 1.0f - Ease01(Segment01(airU, 0.0f, 0.35f));
        const float tuck   = Ease01(Segment01(airU, 0.30f, 0.75f));
        // 收腿是叠在"蹬直"之上的：extend 把它拉回伸直，tuck 把膝收到身前。
        kneeDepth = tune.crouchKnee * extend + tune.airTuckKnee * tuck;
        hipDepth  = tune.crouchHip * extend + tune.airTuckHip * tuck;
        ankle     = tune.crouchAnkle * extend;
        // 手臂上举然后自然下落。
        armRaise = tune.airArmRaise * (1.0f - Ease01(Segment01(airU, 0.45f, 1.0f)));
    } else {
        // 落地：从"腿还收着"的触地姿态迅速弯到最深，再站起来。
        // 这条曲线两段拼：先加深（承受冲击），再归零（站直）。
        const float deepen = Ease01(Segment01(landU, 0.0f, 0.28f));
        const float recover = Ease01(Segment01(landU, 0.30f, 1.0f));
        const float depth = deepen * (1.0f - recover);
        kneeDepth = tune.landKnee * depth;
        hipDepth  = tune.landHip * depth;
        ankle     = tune.landAnkle * depth;
        armRaise  = -0.25f * depth;   // 落地时手臂略微后撑
    }

    // 躯干跟着髋前屈，头保持看向前方（跳的时候没人埋头）。
    rotate(rig_.spine, rig_.side, hipDepth * 0.35f);
    rotate(rig_.chest, rig_.side, hipDepth * 0.20f);
    rotate(rig_.head, rig_.side, -hipDepth * 0.30f);

    // 手臂：先放成 A-pose（这具骨架是 T-pose，见 ProceduralWalk 的同一段说明），
    // 再绕侧向轴做上举/后摆。
    const float armDown = 1.05f;
    rotate(rig_.armUp[kRigLeft], rig_.forward, -armDown);
    rotate(rig_.armUp[kRigRight], rig_.forward, +armDown);
    // 上举：绕侧向轴的**负向**是朝前，再往前就是朝上。两侧对称。
    rotate(rig_.armUp[kRigLeft], rig_.side, -armRaise);
    rotate(rig_.armUp[kRigRight], rig_.side, -armRaise);
    // 屈肘让手臂不是两根直棍。
    const float elbow = -(0.45f + 0.35f * std::max(armRaise, 0.0f));
    rotate(rig_.armLow[kRigLeft], rig_.side, elbow);
    rotate(rig_.armLow[kRigRight], rig_.side, elbow);

    // 腿：左右对称（双脚同时起跳，不是单腿跳）。
    for (int s = 0; s < kRigSides; ++s) {
        rotate(rig_.legUp[s], rig_.side, hipDepth);
        rotate(rig_.legLow[s], rig_.side, kneeDepth);
        rotate(rig_.foot[s], rig_.side, ankle - kneeDepth * 0.45f);
        // 踝被膝盖带平之后，趾骨要补一点背屈，否则脚尖戳进地面。
        rotate(rig_.toe[s], rig_.side, kneeDepth * 0.25f);
    }
}

} // namespace gldx
