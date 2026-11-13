module;

#include "gldx/gmf.hpp"

module gldx;

// 类型全部从模块拿（这些头都在 gldx.cppm 里 export）。**不要**在这里文本 include
// ProceduralWalk.h / RigProfile.h：那会把已导出的实体再解析一遍，报成一串
// redefinition。与 RigNaming.cpp / SkinnedMesh.cpp 是同一条规则。

namespace gldx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// 曲线的构筑块。给它们起名字是为了让下面的步态读起来像步态而不是像算术。
float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
// smoothstep：两端导数为 0，所以拿它拼接的曲线在接缝处不会有可见的折角。
float Ease01(float x) {
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
// 把 x 映射成"在 [a,b] 区间内走到几成"，区间外钳在 0/1。
float Segment01(float x, float a, float b) {
    return Clamp01((x - a) / std::max(b - a, 1e-5f));
}

// 一条腿在归一化相位 p（0 = 脚跟落地）上的四个关节角。
//
// 符号约定全部来自同一处：绕骨架的侧向轴**正向**旋转，会让一根朝下的骨头往**后**摆。
// 所以大腿角在腿朝前时是负的、屈膝是正的（小腿往后）、踝角为正表示脚尖朝下。
struct LegAngles {
    float hip = 0.0f;
    float knee = 0.0f;
    float ankle = 0.0f;
    float toe = 0.0f;
};

LegAngles LegAt(const WalkTuning& tune, float p) {
    LegAngles out;
    if (p < tune.stanceEnd) {
        const float s = p / tune.stanceEnd;                   // 0 = 落地, 1 = 蹬离
        // 支撑相走**线性**而不是缓动：脚是钉在地上的，骨盆以恒定速度从它上方越过，
        // 这里线性的是"髋关节的相对角"。给它加缓动会让角色一顿一顿地窜。
        out.hip = tune.hipSwing * (2.0f * s - 1.0f);
        // 承重反应：落地后膝盖立刻沉一下，到支撑中期（身体最高点）伸直。
        out.knee = tune.kneeStanceFlex * (1.0f - Ease01(Segment01(s, 0.0f, 0.45f)));
        // 脚跟落地（脚尖朝上）先放平，再蹬伸进入推离。
        out.ankle = tune.ankleHeelStrike * (1.0f - Ease01(Segment01(s, 0.0f, 0.30f))) +
                    tune.ankleToeOff * Ease01(Segment01(s, 0.62f, 1.0f));
        // 趾关节只在支撑相最末尾动：这时脚跟已经离地，脚尖是最后一个接触点。
        out.toe = -tune.toeOffBend * Ease01(Segment01(s, 0.70f, 1.0f));
    } else {
        const float s = (p - tune.stanceEnd) / (1.0f - tune.stanceEnd);  // 0 = 蹬离, 1 = 落地
        // 余弦缓动：摆动的肢体在中间加速、在接近下一次落地时减速，这就是余弦的形状。
        const float from = tune.hipSwing;
        const float to = -tune.hipSwing;
        out.hip = from + (to - from) * (0.5f - 0.5f * std::cos(kPi * s));
        // 膝盖：摆动早期猛屈以抬脚过地，然后伸直、在落地时够远。两端都乘一个归零
        // 因子，是这条曲线跨支撑/摆动边界时**连续**的原因——那里若有 0.14 rad 的
        // 台阶，看起来就是明显的"啪"一下。
        //
        // 峰值在摆动**早期**而非正中：膝盖在腿还在身后时抬起把脚带离地面，然后
        // 在腿经过身体下方时伸开。峰在正中会做出"正步走"——脚在身体正下方最高，
        // 没人这么走，而且测出来的鞋底高度会直接告发（改之前 0.31 m，改之后 ~0.06 m）。
        const float swell = Ease01(Segment01(s, 0.0f, 0.22f)) *
                            (1.0f - Ease01(Segment01(s, 0.28f, 0.72f)));
        out.knee = tune.kneeSwingFlex * swell;
        // 蹬离后的跖屈解旋；脚跟落地前背屈。
        out.ankle = tune.ankleToeOff * (1.0f - Ease01(Segment01(s, 0.0f, 0.28f))) +
                    tune.ankleHeelStrike * Ease01(Segment01(s, 0.40f, 0.95f));
        out.toe = -tune.toeOffBend * (1.0f - Ease01(Segment01(s, 0.0f, 0.22f)));
    }
    return out;
}

} // namespace

ProceduralWalk::ProceduralWalk(const Skeleton& skeleton, const RigProfile& rig,
                               const std::vector<glm::mat4>& restWorld,
                               const WalkTuning& tuning, float cycleSeconds)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning),
      duration_(std::max(cycleSeconds, 1e-3f)) {}

void ProceduralWalk::Sample(float t, std::vector<glm::mat4>& outPose) const {
    // 先整组填回 rest local。Pose 是不会累积的：每次重摆都从静止开始叠，否则误差
    // 会累成"越走越歪"，且 --freeze-at 不再可复现。
    ResetToRest(skeleton_, outPose);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const WalkTuning& tune = tuning_;

    // 蹲行：把"弯多少"折进关节角，而不是另写一条曲线。膝盖常态屈曲 + 髋部前屈，
    // 骨盆高度交给调用方的地面接触去解（手写一个下沉量会跟腿部改动打架）。
    const float crouchKnee = tune.crouch * 0.9f;    // rad
    const float crouchHip = tune.crouch * 0.5f;     // rad, 髋前屈，躯干才会跟着低下去
    const float swingScale = 1.0f - 0.45f * tune.crouch;   // 蹲着走步幅自然变小

    // t 折进 0..1。Looping() 为 true，所以这里做取模而不是钳制。
    //
    // 先在**时间域**取模、再除以 duration，而不是先除再 floor：两者在数学上等价，
    // 但在浮点里不等价——`(t+dur)/dur - floor(...)` 与 `t/dur - floor(...)` 会因为
    // 中间某一步的舍入差出一个 ULP，于是"t 与 t+Duration 给出同一姿态"这条循环语义
    // 在个别 t 上不成立。这个等式是状态机做相位比较与回归取证时的前提，不能是近似的。
    float folded = (duration_ > 0.0f) ? std::fmod(t, duration_) : 0.0f;
    if (folded < 0.0f) folded += duration_;         // t 可能为负
    float phase = (duration_ > 0.0f) ? (folded / duration_) : 0.0f;

    const LegAngles left = LegAt(tune, phase);
    const LegAngles right = LegAt(tune, phase < 0.5f ? phase + 0.5f : phase - 0.5f);

    // 躯干即使腿不动也稍微前倾：远看时"在走路"读到的是躯干，脚只有几个像素。
    rotate(rig_.spine, rig_.side, tune.lean + crouchHip * 0.5f);
    rotate(rig_.chest, rig_.side, tune.lean * 0.5f + crouchHip * 0.25f);

    // 这具骨架的 bind pose 是 T-pose：手臂沿侧向轴笔直伸出，所以绕那根轴转等于
    // 让整条手臂绕自身长轴自转——看不见。先把手臂放成 A-pose 是这个动作的一部分
    // （这具骨架没有可继承的 A-pose），之后绕侧向轴的旋转才是前后摆。
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armDown);
    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armDown);

    // 骨盆与肩反向转，头抵掉大部分，角色才会朝着它走的方向看。
    const float yawDriver = left.hip / std::max(tune.hipSwing, 1e-4f);   // -1 = 左腿在前
    rotate(rig_.hips, rig_.up, tune.hipYaw * yawDriver);
    rotate(rig_.chest, rig_.up, -tune.chestYaw * yawDriver);
    rotate(rig_.head, rig_.up, tune.headYaw * yawDriver);

    for (int s = 0; s < kRigSides; ++s) {
        const LegAngles& leg = (s == 0) ? left : right;
        rotate(rig_.legUp[s], rig_.side, leg.hip * swingScale + crouchHip);
        rotate(rig_.legLow[s], rig_.side, leg.knee + crouchKnee);
        rotate(rig_.foot[s], rig_.side, leg.ankle - crouchKnee * 0.55f);
        // 蹲下时趾关节要反过来背屈一点，否则脚尖戳进地面：脚掌被踝关节带平了，
        // 但趾骨的静止姿态仍是平的。
        rotate(rig_.toe[s], rig_.side, leg.toe + crouchKnee * 0.30f);

        // 对侧摆臂：同侧腿往后时手臂往前，正好相反。
        const float arm = -tune.armSwing * (leg.hip / std::max(tune.hipSwing, 1e-4f));
        // 绕侧向轴的负向是"向前"，所以屈肘（把前臂往前弯）也是负的，且手臂前摆时
        // 屈得更深——后摆时直、前面时弯，这才读得出"放松地走"。
        const float swingN = arm / std::max(tune.armSwing, 1e-4f);   // +1 后摆, -1 前摆
        const float elbow = -(tune.elbowBase + tune.elbowSwing * (0.5f - 0.5f * swingN));
        rotate(rig_.armUp[s], rig_.side, arm);
        // 肩胛不是刚性支架：它跟着手臂前后动。三度就够让轮廓不再读成"方块上装着
        // 一条铰接的棍子"。
        rotate(rig_.clavicle[s], rig_.up, -tune.clavicleRoll * swingN);
        rotate(rig_.armLow[s], rig_.side, elbow);
        // ……再加一个恒定的前臂外展。这是把手拿出裙摆的那一环：肱骨没法在不把姿态
        // 摆得又僵又 A 形的前提下离躯干够远，所以间隙在前臂这一级换。
        rotate(rig_.armLow[s], rig_.up, tune.armOut * (s == 0 ? 1.0f : -1.0f));
        // ……以及腕关节，眼睛在肢体末端真正追踪的就是它。没有它整条手臂就是刚性
        // 摆锤：肩和肘在动、手的朝向一直不变，即使手臂在摆也读作"手是冻住的"。
        const float wrist = -(tune.wristBase + tune.wristSwing * (0.5f - 0.5f * swingN));
        rotate(rig_.hand[s], rig_.side, wrist);
        const float deviate = tune.wristDeviate * swingN * (s == 0 ? 1.0f : -1.0f);
        rotate(rig_.hand[s], rig_.up, deviate);
    }
}

} // namespace gldx
