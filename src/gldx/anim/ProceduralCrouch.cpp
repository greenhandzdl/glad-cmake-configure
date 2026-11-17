module;

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿；不文本 include 自己的头（会报 redefinition）。

namespace gldx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// 蹲走的调好值 + `crouch = 1`。本类的腿全部来自它。
WalkTuning MakeCrouchBase(const WalkTuning& walkTuning) {
    WalkTuning t = walkTuning;
    t.crouch = 1.0f;
    return t;
}

} // namespace

ProceduralCrouch::ProceduralCrouch(const Skeleton& skeleton, const RigProfile& rig,
                                   const std::vector<glm::mat4>& restWorld,
                                   const CrouchTuning& tuning, const WalkTuning& walkTuning)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning),
      // 蹲姿的腿来自**蹲走**那条曲线：把它的 crouch 拉满，相位由本类钉住。
      // 同一具骨架、同一份 restWorld，所以两组矩阵可以直接比较——这也是"蹲下就是
      // 蹲走的一帧"能用逐字节 diff 验证的原因。Duration 传 1 只为了满足构造参数，
      // 本类从不按时间采样它（只走 SampleAtPhase）。
      pose_(skeleton, rig, restWorld, MakeCrouchBase(walkTuning), 1.0f) {}

void ProceduralCrouch::Sample(float t, std::vector<glm::mat4>& outPose) const {
    // 1) 腿与躯干：蹲走曲线在固定相位上的一帧。这一步已经写过整个 outPose。
    float phase = tuning_.posePhase;
    phase -= std::floor(phase);                 // 允许调用方传任意相位
    pose_.SampleAtPhase(phase, outPose);

    // 2) 呼吸：只在**上身**叠加。腿一点不动，所以两只脚一直踩在地上，地面解算
    //    每帧解出的抬升量恒定 → 头/肩在几毫米内起伏，重心不飘。
    const float period = std::max(tuning_.breathSeconds, 1e-3f);
    float folded = std::fmod(t, period);
    if (folded < 0.0f) folded += period;
    const float breath = folded / period;               // 0..1
    const float breathe = std::sin(breath * 2.0f * kPi);
    // 一次呼吸里上半身轻微起伏；用呼吸相位而不是另一个频率，循环才闭合。
    const float d = std::clamp(tuning_.depth, 0.0f, 1.0f);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        if (radians == 0.0f) return;
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    rotate(rig_.spine, rig_.side, tuning_.breathSpine * breathe * d);
    rotate(rig_.chest, rig_.side, tuning_.breathSpine * 0.6f * breathe * d);
    rotate(rig_.head, rig_.side, -tuning_.breathHead * breathe * d);
    // 极缓的左右摆动。幅度很小，去掉它画面会"死"，加上它看不出是动画。
    rotate(rig_.spine, rig_.up, tuning_.breathSway * breathe * d);
}

} // namespace gldx
