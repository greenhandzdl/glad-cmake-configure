module;

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿；不文本 include 自己的头（会报 redefinition）。

namespace gldx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// 把 t 折进 [0, period) 再归一化到 [0,1)。先在时间域取模再除——理由同其它动作：
// t 与 t+period 必须给出同一姿态，这是 Sample 作为纯周期函数的定义。
float Cycle01(float t, float period) {
    period = std::max(period, 1e-3f);
    float folded = std::fmod(t, period);
    if (folded < 0.0f) folded += period;
    return folded / period;
}

} // namespace

ProceduralIdle::ProceduralIdle(const Skeleton& skeleton, const RigProfile& rig,
                               const std::vector<glm::mat4>& restWorld,
                               const IdleTuning& tuning)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning) {
    // 三个子周期合成 Duration()：取三者的"最近整数倍"公共周期。
    //
    // 不能简单地取 lcm：三个周期是浮点参数，任意值都有公倍数但可能是几万秒，
    // 那样的"循环"等于不循环。所以把每个周期就近量化成呼吸周期的整数倍
    // （至少 1 倍），公共周期就成了 max(倍数) × 呼吸周期。
    //
    // 量化是必要的而不是偷懒：调用方改了 swaySeconds 却期望"照原样用那个数"，
    // 与"Looping() 必须成立"直接冲突，而后者是接口承诺。所以以呼吸为基准整拍，
    // 改动的幅度被吸收到 swing 的实际周期里——差异在 ±半拍之内，肉眼不可见，
    // 而循环性变成精确的。
    const float b = std::max(tuning_.breathSeconds, 1e-3f);
    auto beats = [b](float seconds) {
        return std::max(1.0f, std::round(std::max(seconds, 1e-3f) / b));
    };
    const float beatsMax = std::max({1.0f, beats(tuning_.swaySeconds),
                                     beats(tuning_.gazeSeconds)});
    duration_ = b * beatsMax;

    // 把三个周期都吸附到整拍上，Sample 里就只需要按 duration_ 统一取模。
    tuning_.breathSeconds = b;
    tuning_.swaySeconds   = b * beats(tuning_.swaySeconds);
    tuning_.gazeSeconds   = b * beats(tuning_.gazeSeconds);
}

void ProceduralIdle::Sample(float t, std::vector<glm::mat4>& outPose) const {
    ResetToRest(skeleton_, outPose);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const IdleTuning& tune = tuning_;

    // 三个独立的周期，互不同拍。用正弦而不是三角波：正弦在端点速度为零，
    // 三角波会在折返处有尖角，而尖角在极小幅度下依然能被眼睛抓到（那是"抽搐"）。
    const float breath = std::sin(Cycle01(t, tune.breathSeconds) * 2.0f * kPi);
    const float sway   = std::sin(Cycle01(t, tune.swaySeconds) * 2.0f * kPi);
    const float gaze   = std::sin(Cycle01(t, tune.gazeSeconds) * 2.0f * kPi);

    // ---- 呼吸 ----
    // 主体在胸腔，脊柱只跟一点点。全给脊柱会让上身像一块板前后点。
    rotate(rig_.chest, rig_.side, tune.breathChest * breath);
    rotate(rig_.spine, rig_.side, tune.breathSpine * breath);
    // 头随呼吸微微点头，方向与胸腔相反（吸气的抬头是胸廓撑开的结果）。
    rotate(rig_.head, rig_.side, -tune.breathSpine * 0.6f * breath);

    // ---- 重心微晃 ----
    // 绕上下轴的极缓慢转动，配合骨盆的侧向倾。两处一起才有"重心在挪"的感觉，
    // 只转骨盆会像下半身自己在动。
    rotate(rig_.hips, rig_.up, tune.swayAmplitude * sway);
    rotate(rig_.spine, rig_.up, -tune.swayAmplitude * 0.5f * sway);

    // ---- 视线游移 ----
    // 只动头，不动躯干：躯干跟着转会读成"转身"，而待机时人只是眼睛和头在动。
    rotate(rig_.head, rig_.up, tune.gazeAmplitude * gaze);
    rotate(rig_.head, rig_.side, -tune.gazeAmplitude * 0.25f * gaze);

    // ---- 手臂 ----
    // 先下放到 A-pose。T-pose 不放下手臂的待机读起来像投降，不是放松。
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armDown);
    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armDown);
    // 略微外展，免得手臂穿进躯干/大衣。
    rotate(rig_.armUp[kRigLeft], rig_.side, -tune.armOut);
    rotate(rig_.armUp[kRigRight], rig_.side, tune.armOut);
    // 肘几乎伸直，只留一点自然弯（完全锁死在数学上很"直"，但看起来像假肢）。
    rotate(rig_.armLow[kRigLeft], rig_.side, -tune.elbowBend);
    rotate(rig_.armLow[kRigRight], rig_.side, -tune.elbowBend);
    // 手腕跟着重力垂一点。
    rotate(rig_.hand[kRigLeft], rig_.side, -0.10f);
    rotate(rig_.hand[kRigRight], rig_.side, -0.10f);

    // ---- 站姿 ----
    // 膝盖留一点微屈。bind pose 是锁死的直腿，而正常人站着时膝关节始终有一点弯，
    // 锁死会读成"立正"。两侧同值，所以不会破坏对称。
    for (int s = 0; s < kRigSides; ++s) {
        rotate(rig_.legUp[s], rig_.side, -tune.stanceKnee * 0.4f);
        rotate(rig_.legLow[s], rig_.side, tune.stanceKnee);
        // 踝补回一半，脚掌才保持平放（不然膝弯会让整只脚抬起）。
        rotate(rig_.foot[s], rig_.side, -tune.stanceKnee * 0.6f);
    }
}

} // namespace gldx
