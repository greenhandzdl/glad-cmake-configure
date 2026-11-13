module;

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿；不文本 include 自己的头（会报 redefinition）。

namespace gldx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float Ease01(float x) {
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
float Segment01(float x, float a, float b) {
    return Clamp01((x - a) / std::max(b - a, 1e-5f));
}

// 一条腿在归一化相位 p 上的四个关节角。符号约定与行走一致（绕侧向轴正向 = 朝后）。
struct LegAngles {
    float hip = 0.0f;
    float knee = 0.0f;
    float ankle = 0.0f;
    float toe = 0.0f;
};

LegAngles LegAt(const RunTuning& tune, float p) {
    LegAngles out;
    // 跑步与走路的关键区别：p >= stanceEnd 之后**不是**立刻进入摆动，而是一段
    // 腾空。两条腿的支撑区间不相接，所以这里把 [stanceEnd, 1) 这段同时承担
    // "蹬离后的收腿"与"伸腿准备下一次触地"两件事。
    if (p < tune.stanceEnd) {
        const float s = p / tune.stanceEnd;      // 0 = 触地, 1 = 蹬离
        // 支撑相仍然线性（脚钉在地上，骨盆匀速越过它）。
        out.hip = tune.hipSwing * (2.0f * s - 1.0f);
        // 落地缓冲比走路深：跑步落地的垂直冲击是体重的 2-3 倍，膝盖必须有行程。
        // 峰值在支撑早期，到蹬离时腿是伸直的（那是推力来源）。
        out.knee = tune.kneeStanceFlex * (1.0f - Ease01(Segment01(s, 0.0f, 0.55f)));
        // 中足落地：背屈很小，然后迅速转入蹬伸。
        out.ankle = tune.ankleHeelStrike * (1.0f - Ease01(Segment01(s, 0.0f, 0.25f))) +
                    tune.ankleToeOff * Ease01(Segment01(s, 0.55f, 1.0f));
        out.toe = -tune.toeOffBend * Ease01(Segment01(s, 0.65f, 1.0f));
    } else {
        const float s = (p - tune.stanceEnd) / (1.0f - tune.stanceEnd);  // 0 = 蹬离, 1 = 触地

        // 摆动相分两半，这是跑步与走路最大的姿态差异：
        //   前半（s < 0.55）：脚跟往臀部收（kneeFlightFlex 打头，收到 kneeSwingFlex）
        //   后半（s >= 0.55）：腿伸出去够下一次触地
        // 走路没有"脚跟踢臀"这个动作，所以这条曲线不能直接照搬。
        const float heelToButt = Ease01(Segment01(s, 0.0f, 0.45f));
        const float reachOut = Ease01(Segment01(s, 0.45f, 1.0f));

        // 髋：从身后（+hipSwing）摆到身前（-hipSwing）。用余弦缓动，两端速度为零，
        // 与支撑相两端的端点速度虽然不同（支撑是线性的），但角度值相等，所以姿态
        // 连续——角度连续比速度连续重要得多，后者只能靠肉眼看出"顿挫"。
        const float from = tune.hipSwing;
        const float to = -tune.hipSwing;
        out.hip = from + (to - from) * (0.5f - 0.5f * std::cos(kPi * s));

        // 膝：先猛收到最大值（脚跟近臀），再在伸腿时放开到接近直立。
        // 两端的归零因子让它在支撑/摆动边界与支撑相的值接上。
        out.knee = tune.kneeSwingFlex * heelToButt * (1.0f - Ease01(Segment01(s, 0.62f, 1.0f))) +
                   tune.kneeFlightFlex * (1.0f - reachOut) * Ease01(Segment01(s, 0.0f, 0.15f));

        // 踝：收腿时跖屈（脚尖朝下，跟着小腿走），伸腿时背屈准备触地。
        out.ankle = tune.ankleToeOff * (1.0f - Ease01(Segment01(s, 0.0f, 0.30f))) +
                    tune.ankleHeelStrike * Ease01(Segment01(s, 0.65f, 1.0f));
        out.toe = -tune.toeOffBend * (1.0f - Ease01(Segment01(s, 0.0f, 0.25f)));
    }
    return out;
}

} // namespace

ProceduralRun::ProceduralRun(const Skeleton& skeleton, const RigProfile& rig,
                             const std::vector<glm::mat4>& restWorld,
                             const RunTuning& tuning, float cycleSeconds)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning),
      duration_(std::max(cycleSeconds, 1e-3f)) {}

float ProceduralRun::AirborneWeightAt(float t) const noexcept {
    // 一条腿的相位。
    float folded = (duration_ > 0.0f) ? std::fmod(t, duration_) : 0.0f;
    if (folded < 0.0f) folded += duration_;
    const float p = (duration_ > 0.0f) ? (folded / duration_) : 0.0f;

    // 两条腿各自的支撑区间（右腿落后半个周期）。某人着地 ⇔ 他的相位落在
    // [0, stanceEnd) 或右腿对应的那一段里。
    auto inStance = [&](float phase) {
        return phase < tuning_.stanceEnd;
    };
    const float pr = (p < 0.5f) ? p + 0.5f : p - 0.5f;
    const bool grounded = inStance(p) || inStance(pr);
    if (grounded) return 0.0f;

    // 腾空段的形状：用"离最近一次触地有多远"做钟形，峰值在腾空正中。
    //
    // 归一化的分母必须是**腾空段自身的一半长度**，不是 1-stanceEnd：腾空段是
    // [stanceEnd, 1)，长度 1-stanceEnd，而钟形只需覆盖它的一半就能到达峰值 1。
    // 早先版本除以整个 1-stanceEnd，于是峰值只到 sin(pi/4)≈0.71——看起来"腾空
    // 权重"永远升不满，而那个数字会被当成地面解算的衰减系数用。
    // 当前处在**哪一条**腾空段里，就用哪条腿的相位做钟形。
    //
    // 早先的写法对两条腿各算一个"到下一次触地的距离"再取 min——那是错的：
    // 两个腾空段（p∈[stanceEnd,0.5) 与 p∈[0.5+stanceEnd,1)）同时"远处"和"近处"
    // 各有一个值，取 min 会让每段腾空一开始就贴着 0，权重永远升不满。钟形必须
    // 只在本段的局部坐标里算。
    //
    // 局部坐标：本段起点是该腿的 stanceEnd，终点是下一次触地（即相位回绕到 0）。
    // 用已经在腾空里的那条腿的相位作为局部进度。
    // 腾空段在两个**半周期**里各自出现一次，每半周期长度 0.5，所以一段腾空的长度
    // 是 (0.5 - stanceEnd)，不是 (1 - stanceEnd)。用后者会把两段腾空当成一段长的
    // 来归一化，钟形因此在每段里只爬到 sin(0.15/0.65*pi) ≈ 0.66 就掉头——峰值
    // 永远到不了 1，而这个数会被当成"地面解算衰减多少"用。
    const bool leftInFlight = (p >= tuning_.stanceEnd && p < 0.5f);
    const float local = leftInFlight ? p : pr;
    const float flightLen = std::max(0.5f - tuning_.stanceEnd, 1e-3f);
    const float u = Clamp01((local - tuning_.stanceEnd) / flightLen);   // 0 = 刚离地, 1 = 即将触地
    // 两端平滑的钟形：刚离地时慢慢升起来，快落地时慢慢压回去。
    return std::sin(u * kPi);
}

void ProceduralRun::Sample(float t, std::vector<glm::mat4>& outPose) const {
    ResetToRest(skeleton_, outPose);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const RunTuning& tune = tuning_;

    // 时间域取模再除（理由见 ProceduralWalk.cpp 同一段）：保证 t 与 t+Duration
    // 给出同一姿态，这是循环语义与回归取证的前提。
    float folded = (duration_ > 0.0f) ? std::fmod(t, duration_) : 0.0f;
    if (folded < 0.0f) folded += duration_;
    const float phase = (duration_ > 0.0f) ? (folded / duration_) : 0.0f;

    const LegAngles left = LegAt(tune, phase);
    const LegAngles right = LegAt(tune, phase < 0.5f ? phase + 0.5f : phase - 0.5f);

    // 躯干前倾：跑步的前倾比走路大得多，且这是"读起来像在跑"的主要线索。
    rotate(rig_.spine, rig_.side, tune.lean);
    rotate(rig_.chest, rig_.side, tune.lean * 0.6f);
    // 头反向仰一点，保持看向前方而不是盯着地面。
    rotate(rig_.head, rig_.side, -tune.lean * 0.55f);

    // 手臂：先放到贴着身体的基准角（跑步不需要走路那么多 A-pose 外展）。
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armDown);
    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armDown);

    const float yawDriver = left.hip / std::max(tune.hipSwing, 1e-4f);
    rotate(rig_.hips, rig_.up, tune.hipYaw * yawDriver);
    // 肩的转动与骨盆相反，且跑步时这个反向更明显。
    rotate(rig_.chest, rig_.up, -tune.chestYaw * yawDriver);
    rotate(rig_.head, rig_.up, tune.headYaw * yawDriver);

    for (int s = 0; s < kRigSides; ++s) {
        const LegAngles& leg = (s == 0) ? left : right;
        rotate(rig_.legUp[s], rig_.side, leg.hip);
        rotate(rig_.legLow[s], rig_.side, leg.knee);
        rotate(rig_.foot[s], rig_.side, leg.ankle);
        rotate(rig_.toe[s], rig_.side, leg.toe);

        // 摆臂：对侧。跑步的摆幅比走路大，且肘角**基本固定**——走路时肘随摆动
        // 屈伸，跑步时它锁在约 90°，靠肩在摆。这个差别正是"走"与"跑"的辨识点之一。
        const float armSwingNorm = -(leg.hip / std::max(tune.hipSwing, 1e-4f));
        rotate(rig_.armUp[s], rig_.side, tune.armSwing * armSwingNorm);
        rotate(rig_.armLow[s], rig_.side, -tune.elbowHold);
        // 前臂略微外展，免得手扫到大腿。
        rotate(rig_.armLow[s], rig_.up, 0.06f * (s == 0 ? 1.0f : -1.0f));
        // 腕跟着摆一点点，不锁死。
        rotate(rig_.hand[s], rig_.side, -(0.10f + 0.12f * (0.5f - 0.5f * armSwingNorm)));
    }
}

} // namespace gldx
