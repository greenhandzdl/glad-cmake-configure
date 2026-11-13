module;

#include "gldx/gmf.hpp"

module gldx;

// 类型从模块拿；不文本 include 自己的头（会报 redefinition）。

namespace gldx {
namespace {

constexpr float kPi = 3.14159265358979323846f;

} // namespace

ProceduralCrouch::ProceduralCrouch(const Skeleton& skeleton, const RigProfile& rig,
                                   const std::vector<glm::mat4>& restWorld,
                                   const CrouchTuning& tuning)
    : skeleton_(skeleton), rig_(rig), restWorld_(restWorld), tuning_(tuning) {}

void ProceduralCrouch::Sample(float t, std::vector<glm::mat4>& outPose) const {
    ResetToRest(skeleton_, outPose);

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        RotateBone(outPose, restWorld_, bone, worldAxis, radians);
    };

    const CrouchTuning& tune = tuning_;

    // 呼吸相位。Duration() 就是一次呼吸的长度，所以这里直接用 t 折呼吸周期。
    const float period = std::max(tune.breathSeconds, 1e-3f);
    float folded = std::fmod(t, period);
    if (folded < 0.0f) folded += period;
    const float breath = folded / period;              // 0..1
    // 一次呼吸：吸气（胸腔扩）在 0..0.5，呼气在 0.5..1。
    const float breathPhase = std::sin(breath * 2.0f * kPi);
    // 重心摆动用半周期，所以它是"呼吸两次、晃一个来回"，不会和呼吸同拍。
    const float swayPhase = std::sin(breath * kPi);

    const float d = std::clamp(tune.depth, 0.0f, 1.0f);

    // 躯干：前倾 + 呼吸起伏。前倾是蹲姿的必要条件——上身直立配合深屈膝会让
    // 重心落在脚跟之外，视觉上像要往后倒。
    rotate(rig_.spine, rig_.side, tune.spineLean * d + tune.breathSpine * breathPhase);
    rotate(rig_.chest, rig_.side, tune.spineLean * 0.35f * d + tune.breathSpine * 0.6f * breathPhase);
    // 头反向仰起，保持看向前方。
    rotate(rig_.head, rig_.side, -tune.headLift * d);
    // 极缓的左右摆动。幅度很小，去掉它画面会"死"，加上它看不出是动画。
    rotate(rig_.spine, rig_.up, tune.breathSway * swayPhase);

    // 手臂：先下放，再略往前、屈肘。抱膝需要手指，参考资产没有，所以选择垂在身侧。
    rotate(rig_.armUp[kRigLeft], rig_.forward, -tune.armDown * d);
    rotate(rig_.armUp[kRigRight], rig_.forward, +tune.armDown * d);
    rotate(rig_.armUp[kRigLeft], rig_.side, -tune.armForward * d);
    rotate(rig_.armUp[kRigRight], rig_.side, -tune.armForward * d);
    rotate(rig_.armLow[kRigLeft], rig_.side, -tune.elbowBend * d);
    rotate(rig_.armLow[kRigRight], rig_.side, -tune.elbowBend * d);
    // 前臂略微内收，蹲着时手臂贴近身体。
    rotate(rig_.armLow[kRigLeft], rig_.up, 0.10f * d);
    rotate(rig_.armLow[kRigRight], rig_.up, -0.10f * d);

    // 腿：髋、膝、踝一起弯。踝的背屈是**必须**的——胫骨前倾时若踝不动，
    // 脚掌会立起来（只有脚跟或只有脚尖着地），而地面解算会把这个错误
    // 变成"整具角色上下漂"。
    for (int s = 0; s < kRigSides; ++s) {
        rotate(rig_.legUp[s], rig_.side, tune.hipBend * d);
        rotate(rig_.legLow[s], rig_.side, tune.kneeBend * d);
        rotate(rig_.foot[s], rig_.side, tune.ankleBend * d);
        // 踝被带平之后趾骨要补背屈，否则脚尖戳进地面。
        rotate(rig_.toe[s], rig_.side, tune.kneeBend * 0.28f * d);
        // 蹲下时大腿略外展，膝盖才会打开而不是顶在一起（人蹲下自然如此，
        // 也是这具角色的裙子不会夹在两腿之间的原因）。
        rotate(rig_.legUp[s], rig_.up, 0.16f * d * (s == 0 ? 1.0f : -1.0f));
    }
}

} // namespace gldx
