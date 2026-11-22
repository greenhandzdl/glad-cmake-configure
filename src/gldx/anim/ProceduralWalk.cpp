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

/**
 * 一条腿在蹲姿底座下的四个角（不带动画 → 这就是"静止蹲姿"="蹲走动画的一帧"）。
 *
 * 这个函数存在的唯一理由：`ProceduralCrouch` 与 `ProceduralWalk::crouch` 必须是
 * **同一条曲线**。曾经它们各写一份常量，修好一边另一边照样错——蹲下与蹲走之间的
 * 切换于是有 2.5 cm 的脚部跳变，看起来像在台阶上打滑。
 *
 * 角度都是"相对步态零位"的偏移：髋前屈（负）、膝屈（正）、踝抵消（负）。
 * `knee = 2 * hip` 这条比例把踝关节放到髋正下方——大腿与小腿等长时两项相消，
 * 于是骨盆蹲在脚上方而不是脚后方（推导见 ProceduralCrouch.cpp 的注释）。
 */
struct CrouchBase {
    float hip = 0.0f;
    float knee = 0.0f;
    float ankle = 0.0f;
};

CrouchBase CrouchBaseFor(const WalkTuning& tune, float amount) {
    CrouchBase base;
    const float a = std::clamp(amount, 0.0f, 1.0f);
    base.hip   = -a * tune.crouchHip;    // rad, 前屈（负）
    base.knee  =  a * tune.crouchKnee;   // rad, = 2 * |hip|
    base.ankle = -(base.knee + base.hip);   // 抵消小腿的世界倾角，脚掌才会平
    return base;
}

} // namespace

ProceduralWalk::ProceduralWalk(const Skeleton& skeleton, const RigProfile& rig,
                               const std::vector<glm::mat4>& restWorld,
                               const WalkTuning& tuning, float cycleSeconds)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning),
      duration_(std::max(cycleSeconds, 1e-3f)) {}

void ProceduralWalk::SampleAtPhase(float phase, std::vector<glm::mat4>& outPose) const {
    // 先整组填回 rest local。Pose 是不会累积的：每次重摆都从静止开始叠，否则误差
    // 会累成"越走越歪"，且 --freeze-at 不再可复现。
    ResetToRest(skeleton_, outPose);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const WalkTuning& tune = tuning_;

    // ---- 蹲行 = 蹲姿底座 + 步态 ----
    //
    // 底座是一条**纯函数**（CrouchBaseFor），调用方可以把相位固定在 0.25 用它画
    // "静止蹲姿"——`ProceduralCrouch` 就是这么做的。两者共用同一条曲线是刻意的：
    // 蹲下与蹲走之间切换时不该有半帧的跳变。
    const CrouchBase crouch = CrouchBaseFor(tune, tune.crouch);
    // 步态幅度随蹲深收窄：蹲着迈不了大步。`StrideScale()` 把同一个比例报给调用方，
    // 让它同步缩地面滚动，否则脚会滑。
    const float gait = 1.0f - 0.45f * tune.crouch;

    const LegAngles left = LegAt(tune, phase);
    const LegAngles right = LegAt(tune, phase < 0.5f ? phase + 0.5f : phase - 0.5f);
    // 后半周期抬得更高：脚要越过另一条腿，而蹲姿下踝关节在髋正下方，间隙只剩那点。
    const float swingLift[2] = {1.0f, 1.0f + 0.55f * tune.crouch};

    // 躯干前倾。绕 side 的**正**向是往前（与腿相反：腿朝下，躯干朝上）。蹲得越深
    // 上身前倾越多，头再反向抬回来，眼睛才看着前方而不是看着地面。
    //
    // 但前倾必须**远小于**髋前屈。原来这两条系数是 0.45 / 0.22，等于把 0.72 rad 的
    // 髋弯曲几乎原样搬到躯干上：脊柱 0.32 + 胸 0.16 再叠 `tune.lean`，正视看是蹲着，
    // 侧视看是弯腰鞠躬——头几乎顶到膝盖，重心跑到脚前面。正视截图里这条被裙摆挡住
    // 了，只有把人转 90° 才看得出来。髋已经把重心降下去了，躯干只需要一点点前倾去
    // 平衡，不是跟着髋一起折。
    const float leanIn = -crouch.hip;    // 0..0.72 的正量
    rotate(rig_.spine, rig_.side, tune.lean + leanIn * 0.22f);
    rotate(rig_.chest, rig_.side, tune.lean * 0.5f + leanIn * 0.10f);
    rotate(rig_.head, rig_.side, -leanIn * 0.26f);

    // 这具骨架的 bind pose 是 T-pose：手臂沿侧向轴笔直伸出，所以绕那根轴转等于
    // 让整条手臂绕自身长轴自转——看不见。先把手臂放成 A-pose 是这个动作的一部分
    // （这具骨架没有可继承的 A-pose），之后绕侧向轴的旋转才是前后摆。
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armDown);
    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armDown);
    // Clear the skirt by raising the arms slightly, NOT by abducting them.
    //
    // The first attempt rotated about `side`, which for a hanging arm is its own long
    // axis: the arm spun instead of moving out, and the visible result was a palm facing
    // the sky. `forward` is perpendicular to a hanging arm, so this actually lifts it
    // away from the hem - and leaves the hand's own orientation alone.
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armOut);
    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armOut);

    // 骨盆与肩反向转，头抵掉大部分，角色才会朝着它走的方向看。
    const float yawDriver = left.hip / std::max(tune.hipSwing, 1e-4f);   // -1 = 左腿在前
    rotate(rig_.hips, rig_.up, tune.hipYaw * yawDriver);
    rotate(rig_.chest, rig_.up, -tune.chestYaw * yawDriver);
    rotate(rig_.head, rig_.up, tune.headYaw * yawDriver);

    for (int s = 0; s < kRigSides; ++s) {
        const LegAngles& leg = (s == 0) ? left : right;
        // 步态的摆动量按蹲深收窄；底座的静止角原样加上去。
        const float lift = swingLift[s];
        rotate(rig_.legUp[s], rig_.side, leg.hip * gait + crouch.hip);
        rotate(rig_.legLow[s], rig_.side, leg.knee * gait * lift + crouch.knee);
        // 踝：步态自己的踝角 + 抵消小腿世界倾角的那一项。倾角是"髋 + 膝"的签名和，
        // 所以补的是 -(hip + knee) —— 少这一项时只有鞋跟着地，地面解算把整具角色
        // 抬起来，蹲姿就永远蹲不下去（实测 0.24 m）。
        rotate(rig_.foot[s], rig_.side, leg.ankle + crouch.ankle);
        // 蹲着时趾骨背屈：鞋底前掌本来会压进地面。
        rotate(rig_.toe[s], rig_.side, leg.toe - crouch.knee * 0.10f);

        // 对侧摆臂：同侧腿往后时手臂往前，正好相反。
        const float arm = -tune.armSwing * (leg.hip / std::max(tune.hipSwing, 1e-4f));
        // 绕侧向轴的负向是"向前"，所以屈肘（把前臂往前弯）也是负的，且手臂前摆时
        // 屈得更深——后摆时直、前面时弯，这才读得出"放松地走"。
        const float swingN = arm / std::max(tune.armSwing, 1e-4f);   // +1 后摆, -1 前摆
        const float elbow = -(tune.elbowBase + tune.elbowSwing * (0.5f - 0.5f * swingN));
        // 蹲着时手臂前伸一点、肘更弯：垂手会扫到裙摆。
        const float armReach = -0.30f * tune.crouch;
        rotate(rig_.armUp[s], rig_.side, arm + armReach);
        // 肩胛不是刚性支架：它跟着手臂前后动。三度就够让轮廓不再读成"方块上装着
        // 一条铰接的棍子"。
        rotate(rig_.clavicle[s], rig_.up, -tune.clavicleRoll * swingN);
        // Elbow bend must be MIRRORED between the sides.
        //
        // `side` points the same way for both arms, so a single signed angle bends one
        // elbow forward and the other backward - the right hand ended up tucked toward
        // the body (measured wrist offset +0.067 m on the right vs -0.135 m on the
        // left) and went straight into the skirt. Mirroring the angle is what makes the
        // two elbows bend the same way in body terms.
        const float sideSign2 = (s == 0) ? 1.0f : -1.0f;
        rotate(rig_.armLow[s], rig_.side, (elbow - 0.30f * tune.crouch) * sideSign2);
        // (The clearance used to be applied here as a forearm rotation about `up`, and
        // on top of the new upper-arm lift it pulled the two sides apart: measured wrist
        // offsets -0.130 / +0.054 m, i.e. one hand clear and one still inside the hem.
        // Clearance belongs at the shoulder only.)
        // ……以及腕关节，眼睛在肢体末端真正追踪的就是它。没有它整条手臂就是刚性
        // 摆锤：肩和肘在动、手的朝向一直不变，即使手臂在摆也读作"手是冻住的"。
        const float wrist = -(tune.wristBase + tune.wristSwing * (0.5f - 0.5f * swingN));
        rotate(rig_.hand[s], rig_.side, wrist);
        const float deviate = tune.wristDeviate * swingN * (s == 0 ? 1.0f : -1.0f);
        rotate(rig_.hand[s], rig_.up, deviate);
    }
}

void ProceduralWalk::Sample(float t, std::vector<glm::mat4>& outPose) const {
    // t 折进 0..1。Looping() 为 true，所以这里做取模而不是钳制。
    //
    // 先在**时间域**取模、再除以 duration，而不是先除再 floor：两者在数学上等价，
    // 但在浮点里不等价——`(t+dur)/dur - floor(...)` 与 `t/dur - floor(...)` 会因为
    // 中间某一步的舍入差出一个 ULP，于是"t 与 t+Duration 给出同一姿态"这条循环语义
    // 在个别 t 上不成立。这个等式是状态机做相位比较与回归取证时的前提，不能是近似的。
    float folded = (duration_ > 0.0f) ? std::fmod(t, duration_) : 0.0f;
    if (folded < 0.0f) folded += duration_;         // t 可能为负
    const float phase = (duration_ > 0.0f) ? (folded / duration_) : 0.0f;
    SampleAtPhase(phase, outPose);
}

} // namespace gldx
