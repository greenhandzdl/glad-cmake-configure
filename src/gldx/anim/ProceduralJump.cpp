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
    const float anim_transit_a_c = c + 0.01 * a; // 第一段动画僵直修改，这个对动画有断裂的效果，不是很好（比较rude)
    // 钳住而不是回绕：不循环的动作在 t 超过时长后停在最后一帧（站直）。
    const float tt = std::clamp(t, 0.0f, duration_);

    // 三段各自的进度（只有所在段是 0..1，其余钳在 0/1）。`crouchU == 1` 同时也
    // 标出"脚刚离地"这一刻，`landU == 0` 标出"脚刚触地"。
    const float crouchU = Segment01(tt, 0.0f, anim_transit_a_c);
    const float airU    = Segment01(tt, c, c + a);
    const float landU   = Segment01(tt, c + a, duration_);

    // ---- 折腿量：四段之间**连续** ----
    //
    // 蓄力段不是"弯到最深就结束"，而是**蹲下再蹬出去**：先蹲（由 crouchHip/Knee 给，
    // knee = 2*hip 让踝关节落在髋正下方），再蹬回 0。蹬直的瞬间正好是离地那一刻——
    // 也正好是 `GroundedAt()` 由 true 变 false 的那一帧。所以离地姿态是**直腿**，
    // 与调用方用来当基准的"站立高度"完全一致，接缝处没有台阶。
    //
    // 腾空段用"两端为 0 的驼峰"，所以离地与触地两帧也都是直腿；中段才是劈叉。
    float hip = 0.0f;       // 大小（屈多少），符号在下面按前/后腿分别给
    float knee = 0.0f;
    float frontBlend = 0.0f;  // 0 = 双腿对称（蹲/落地），1 = 完全劈叉（腾空峰值）
    float armSpread = 0.0f;   // rad，正 = 向两侧上方张开
    float armBack = 0.0f;     // rad，正 = 后摆
    float arch = 0.0f;        // rad，躯干后仰
    float crouchBlend = 0.0f; // 0..1，当前有多"蹲着"（决定两腿张开的量）

    // 蹬伸的缓动曲线：前 62% 蹲到底，之后蹬出去。smoothstep 两端导数为 0，所以
    // 起跳前有一小段"蹲到最深停一拍"的蓄力感，而不是匀速地上去下来。
    // 蓄力曲线：蹲下，再用**最后一段**蹬直。
    //
    // 这里有一个读作「站立卡顿」的坑：`getUp` 在 `crouchU = 1` 才归零，而
    // `crouchU = 1` 正是**离地那一刻**，所以蹬直与离地之间没有重叠——姿态先站直，
    // 上升才开始。实测 t=0.28→0.30 这几帧角色是完全静止的直立姿势，看起来像
    // 卡了一下。把蹬伸的终点提前到 0.88，让「腿伸直」与「开始上升」重叠 12%，
    // 起跳才有连贯的推地感。
    const float squatDown = Ease01(Segment01(crouchU, 0.20f, 0.55f));
    const float driveUp   = Ease01(Segment01(crouchU, 0.55f, 1.00f)); // 不对，应该调低末尾动画曲线
    const float getUp     = squatDown * (1.0f - driveUp);   // 0 -> 最深 -> 0

    if (tt < anim_transit_a_c) {
        hip  = tune.crouchHip * getUp;
        knee = tune.crouchKnee * getUp;
        armBack = tune.armBack * getUp;
        // 蓄力时手臂向后下方摆（甩臂的第一半）。
        armSpread = -0.25f * getUp;
        crouchBlend = getUp;
    } else
        if (tt < c + a) {
        // 腾空：中段劈叉 + 展臂。`split` 是两端为 0 的驼峰，两端都取 0 才能与
        // 蓄力末帧（直腿）和落地首帧（直腿）无缝接上。
        const float split = Ease01(Segment01(airU, 0.08f, 0.45f)) *
                            (1.0f - Ease01(Segment01(airU, 0.72f, 0.95f)));
        frontBlend = split;
        hip  = 0.0f;   // 由 frontBlend 分给两条腿（见下面的腿循环）
        knee = 0.0f;
        armSpread = tune.airArmSpread * split;
        arch = tune.airArch * split;
    } else {
        // 落地：触地时直腿 -> 深屈吸收冲击 -> 站起。两端为 0 的驼峰，所以触地那一帧
        // 和动作结束那一帧都是直腿。
        const float absorb = Ease01(Segment01(landU, 0.0f, 0.30f)) *
                             (1.0f - Ease01(Segment01(landU, 0.45f, 1.0f)));
        hip  = tune.landHip * absorb;
        knee = tune.landKnee * absorb;
        crouchBlend = absorb;
        // 落地时手臂前伸一点接住身体。
        armSpread = -tune.armLand * absorb;
    }

    // ---- 躯干 ----
    // 屈髋会让上身整体前倾，头再反向抬回来（跳的时候没人埋头）；腾空时挺胸。
    rotate(rig_.spine, rig_.side, hip * 0.30f - arch);
    rotate(rig_.chest, rig_.side, hip * 0.15f - arch * 0.6f);
    rotate(rig_.head, rig_.side, -hip * 0.40f + arch * 0.5f);

    // ---- 手臂 ----
    // 这具骨架是 T-pose，先把手臂放成 A-pose（1.05 rad）——**注意这就是"双臂向侧面
    // 展开"的基准**：A-pose 里手臂本来就朝斜下方，腾空要的是把它抬到斜上方，所以
    // 只要往回收一点降幅，而不是从"垂下"再举起来。
    const float armDown = 1.05f;
    rotate(rig_.armUp[kRigLeft], rig_.forward, -armDown);
    rotate(rig_.armUp[kRigRight], rig_.forward, +armDown);
    for (int s = 0; s < kRigSides; ++s) {
        // 这一步画"双臂张开"。
        //
        // `armSpread` **本身就是张开量**（腾空段 = airArmSpread * split），不是
        // "相对 A-pose 回收多少"——写成后者会变成 1.05²≈1.1 rad 的二次方，看着像
        // 张开、实际每次都被削掉一半，实测腾空帧的手臂和站立时几乎一样。
        //
        // 轴的选择同样踩过：T-pose 里上臂指向 +side，绕 **side** 转是绕手臂自身长轴
        // 自旋（看不见），绕 **forward** 才是把它从"侧平举"抬到"斜上举"。两侧镜像。
        const float spread = std::max(armSpread, 0.0f);
        rotate(rig_.armUp[s], rig_.forward, spread * (s == 0 ? -1.0f : 1.0f));
        // 再绕竖直轴加一点上举，手臂才不会只是横向一条线。
        rotate(rig_.armUp[s], rig_.up,
               -tune.airArmRaise * std::min(spread / std::max(tune.airArmSpread, 1e-3f), 1.0f) *
                   (s == 0 ? 1.0f : -1.0f));
        // 蓄力段的后摆（负的 armSpread）仍然走 side 轴。
        rotate(rig_.armUp[s], rig_.side, -armBack);
        const float elbow = -(0.35f + tune.airElbow * std::max(armSpread, 0.0f));
        rotate(rig_.armLow[s], rig_.side, elbow);
    }

    // ---- 腿 ----
    // 腾空是**对称**的垂直跳：两条腿同一个姿态（略屈膝、脚背绷直），不再分红左
    // 右前后来劈叉。落地/蓄力两段 airBlend = 0，退回"双腿对称的屈膝"。
    for (int s = 0; s < kRigSides; ++s) {
        // 两条腿用同一组角；只有"略微外展"按侧别镜像，让正面看得出是两条腿。
        const float h = hip + tune.airHip * frontBlend;
        const float k = knee + tune.airKnee * frontBlend;
        rotate(rig_.legUp[s], rig_.side, -h);
        rotate(rig_.legLow[s], rig_.side, k);
        // 两腿**打开**：蓄力/蹬伸段用 `stanceSplit`，腾空段再加 `airLegSplit`。
        //
        // 之前只在腾空时张开，于是起跳准备看起来像"并腿直立"——人准备起跳时是把两腿
        // 分到肩宽、重心压在两脚之间的。张开同时也是**重心稳定**的来源：支撑多边形
        // 从一条线变成一个面，地面求解出的骨盆高度才不会左右晃。
        const float split = tune.stanceSplit * crouchBlend + tune.airLegSplit * frontBlend;
        if (split > 0.0f) {
            rotate(rig_.legUp[s], rig_.forward, split * (s == 0 ? 1.0f : -1.0f));
        }
        // 踝：**一次**旋转，抵消小腿的世界倾角（脚掌才平），腾空段再叠加绷直量。
        //
        // 这里曾经写成两次独立的 rotate：先 `(h-k)*0.55 + airAnkle*air`，再
        // `(h-k)*0.55`。两次都作用在同一根脚骨上，所以脚掌被多转了整整一遍——
        // 后果不是"脚看起来怪"，而是**起跳/落地时只有脚跟或脚尖着地**，地面解算
        // 据此算出的骨盆高度每帧都在跳，读起来就是"重心不稳"。
        rotate(rig_.foot[s], rig_.side, (h - k) * 0.55f + tune.airAnkle * frontBlend);
        // 折得深时鞋底前掌会压进地面，趾骨补一点背屈。
        rotate(rig_.toe[s], rig_.side, k * 0.10f);
    }
}

} // namespace gldx
