module;

#include "gldx/gmf.hpp"

#include <cmath>
#include <initializer_list>

module gldx;

namespace gldx {

namespace {

// 第一个命中的名字胜出。别名表内部的顺序就是优先级：先列资产自己那一族的写法，
// 再列别的族的等价写法——同一个骨架里真出现两个候选（例如同时有 `Spine` 和
// `Spine1`）时，先列的那个才是这个资产的主力骨骼。
int FindBone(const Skeleton& skeleton, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const int index = skeleton.Find(name);
        if (index >= 0) return index;
    }
    return -1;
}

// 骨骼在 bind 姿态下的世界位置。用 rest world 的第 4 列（平移），不是当前姿态：
// 分类只在加载后做一次，那时候还没有 pose，而且轴的量法必须与"资产自己的静止姿态"
// 一致，否则把 T-pose 摆成 A-pose 之后同一个骨架会量出不同的 up。
glm::vec3 BonePosition(const Skeleton& skeleton, int bone) {
    return bone < 0 ? glm::vec3(0.0f) : glm::vec3(skeleton.RestWorld(bone)[3]);
}

// 轴长判据：1e-4 m 远小于任何人形骨架的真实骨长（几个厘米起），又远大于 float 在
// 1 m 量级上的噪声，所以它只会在"这根骨头根本没绑上/两骨重合"时触发。
inline constexpr float kAxisEpsilon = 1e-4f;

} // namespace

RigProfile RigProfile::Classify(const Skeleton& skeleton) {
    RigProfile rig;

    // ---- 躯干 -----------------------------------------------------------------
    // 注意 `spine` 的别名表里带着 "Spine1"，`spine1` 的别名表里带着 "Spine2"/"Chest"。
    // 这不是抄错，是刻意的降级链：只有两节脊柱的骨架（Spine1/Spine2 命名）用 Spine1 当
    // 腰、Spine2 当胸；三节以上的骨架（Spine/Spine1/Spine2）则由前面的名字先命中。
    // 代价是：两节脊柱时 `spine` 与 `spine1` 会指向同一根骨头，两处旋转会叠加。
    rig.hips  = FindBone(skeleton, {"Hips", "hips", "Pelvis", "pelvis", "Bip01 Pelvis",
                                    // 纯 Mixamo 导出会给**每一根**骨头加 `mixamorig:` 前缀；
                                    // 原 demo 表只给四肢加了前缀（它自己的资产是无前缀的
                                    // Blender FBX），于是原样搬到别的 Mixamo 资产上时
                                    // hips/head 会静默绑不上，walkable 直接变 false。
                                    "mixamorig:Hips"});
    rig.spine = FindBone(skeleton, {"Spine", "spine", "Spine1", "Bip01 Spine", "mixamorig:Spine", "spine_01"});
    rig.chest = FindBone(skeleton, {"Spine2", "Chest", "chest", "spine.003", "Bip01 Spine2",
                                    "mixamorig:Spine2", "spine_03"});
    rig.head  = FindBone(skeleton, {"Head", "head", "Bip01 Head", "mixamorig:Head", "head_01"});
    // 躯干中段，只用来把裙摆/腰线的间隙探针挂上去，不参与断言。
    rig.spine1 = FindBone(skeleton, {"Spine1", "Spine2", "Chest", "mixamorig:Spine1",
                                     "Bip01 Spine1", "mixamorig:Spine2", "spine_02"});

    // ---- 腿 -------------------------------------------------------------------
    rig.legUp[kRigLeft]  = FindBone(skeleton, {"LeftUpLeg", "LeftUpperLeg", "LeftThigh", "UpperLeg.L",
                                               "Thigh.L", "mixamorig:LeftUpLeg", "Bip01 L Thigh", "thigh_l"});
    rig.legLow[kRigLeft] = FindBone(skeleton, {"LeftLeg", "LeftLowerLeg", "LowerLeg.L", "Calf.L",
                                               "Shin.L", "mixamorig:LeftLeg", "Bip01 L Calf", "calf_l"});
    rig.foot[kRigLeft]   = FindBone(skeleton, {"LeftFoot", "Foot.L", "mixamorig:LeftFoot",
                                               "Bip01 L Foot", "foot_l"});
    rig.toe[kRigLeft]    = FindBone(skeleton, {"LeftToeBase", "LeftToe", "Toe.L", "mixamorig:LeftToeBase",
                                               "Bip01 L Toe0", "ball_l"});
    rig.legUp[kRigRight]  = FindBone(skeleton, {"RightUpLeg", "RightUpperLeg", "RightThigh", "UpperLeg.R",
                                                "Thigh.R", "mixamorig:RightUpLeg", "Bip01 R Thigh", "thigh_r"});
    rig.legLow[kRigRight] = FindBone(skeleton, {"RightLeg", "RightLowerLeg", "LowerLeg.R", "Calf.R",
                                                "Shin.R", "mixamorig:RightLeg", "Bip01 R Calf", "calf_r"});
    rig.foot[kRigRight]   = FindBone(skeleton, {"RightFoot", "Foot.R", "mixamorig:RightFoot",
                                                "Bip01 R Foot", "foot_r"});
    rig.toe[kRigRight]    = FindBone(skeleton, {"RightToeBase", "RightToe", "Toe.R", "mixamorig:RightToeBase",
                                                "Bip01 R Toe0", "ball_r"});

    // ---- 臂 -------------------------------------------------------------------
    rig.armUp[kRigLeft]  = FindBone(skeleton, {"LeftArm", "LeftUpperArm", "UpperArm.L", "Arm.L",
                                               "mixamorig:LeftArm", "Bip01 L UpperArm", "upperarm_l"});
    rig.armLow[kRigLeft] = FindBone(skeleton, {"LeftForeArm", "LeftLowerArm", "LowerArm.L", "ForeArm.L",
                                               "mixamorig:LeftForeArm", "Bip01 L Forearm", "lowerarm_l"});
    rig.armUp[kRigRight]  = FindBone(skeleton, {"RightArm", "RightUpperArm", "UpperArm.R", "Arm.R",
                                                "mixamorig:RightArm", "Bip01 R UpperArm", "upperarm_r"});
    rig.armLow[kRigRight] = FindBone(skeleton, {"RightForeArm", "RightLowerArm", "LowerArm.R", "ForeArm.R",
                                                "mixamorig:RightForeArm", "Bip01 R Forearm", "lowerarm_r"});
    rig.hand[kRigLeft]  = FindBone(skeleton, {"LeftHand", "Hand.L", "mixamorig:LeftHand",
                                              "Bip01 L Hand", "hand_l"});
    rig.hand[kRigRight] = FindBone(skeleton, {"RightHand", "Hand.R", "mixamorig:RightHand",
                                              "Bip01 R Hand", "hand_r"});
    rig.clavicle[kRigLeft]  = FindBone(skeleton, {"LeftShoulder", "LeftClavicle", "Shoulder.L", "Clavicle.L",
                                                  "mixamorig:LeftShoulder", "Bip01 L Clavicle", "clavicle_l"});
    rig.clavicle[kRigRight] = FindBone(skeleton, {"RightShoulder", "RightClavicle", "Shoulder.R", "Clavicle.R",
                                                  "mixamorig:RightShoulder", "Bip01 R Clavicle", "clavicle_r"});
    // 没有 `Neck` 槽位：颈只影响"头看向哪"，步态里由 head 槽位承担，多一个槽位就多一处
    // 会漏绑的地方，而漏绑是静默的（见文件头）。

    // ---- 轴：量出来，不假设 ------------------------------------------------
    // up = 髋→头。这是唯一一个与"导出器是 Y-up 还是 Z-up"无关的直立方向定义。
    if (rig.hips >= 0 && rig.head >= 0) {
        const glm::vec3 d = BonePosition(skeleton, rig.head) - BonePosition(skeleton, rig.hips);
        if (glm::length(d) > kAxisEpsilon) rig.up = glm::normalize(d);
    }

    // side = 左大腿 − 右大腿，即指向角色左侧。用减法的方向而不是"左大腿自己的位置"，
    // 是因为位置是相对原点的，两腿中点并不在原点。
    if (rig.legUp[kRigLeft] >= 0 && rig.legUp[kRigRight] >= 0) {
        const glm::vec3 d = BonePosition(skeleton, rig.legUp[kRigLeft]) -
                            BonePosition(skeleton, rig.legUp[kRigRight]);
        if (glm::length(d) > kAxisEpsilon) rig.side = glm::normalize(d);
    } else if (rig.hips >= 0) {
        // 没有腿可量（上半身资产、或腿名没绑上）：只能随便挑一个与 up 垂直的侧轴。
        // seed 的选择只是为了让 cross 结果不是零向量——up 竖直时用 X 当 seed，up 躺平
        // （|up.y| 很小）时用 Y，否则 cross 会退化成零向量再被 normalize 成 NaN。
        const glm::vec3 seed = std::abs(rig.up.y) > 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                         : glm::vec3(0.0f, 1.0f, 0.0f);
        rig.side = glm::normalize(glm::cross(rig.up, seed));
    }
    // 这里有个**保留原行为**的坑，值得写下来：上面两个分支是 if / else if，所以"腿绑上了
    // 但两腿位置重合"（退化资产、或 RestWorld 读到全零）走的是第一个分支、长度不够、side
    // 原样留在默认 (1,0,0)，**不会**落进"没有腿"的兜底分支。默认侧轴只在角色朝 +Z 时才
    // 恰好正确，否则整个步态绕着一根歪掉的轴摆，而且画面是"能走但走路很怪"，不是报错。

    // forward = cross(side, up) 补成右手三系（side 指向角色左侧时，forward 正是它面向的方向）。
    const glm::vec3 forward = glm::cross(rig.side, rig.up);
    if (glm::length(forward) > kAxisEpsilon) {
        rig.forward = glm::normalize(forward);
    }
    // 长度不够意味着 side 与 up 平行，三系已经不可恢复。此处**不能让 normalize 去处理零向量**：
    // glm 的 normalize(0) 得到 NaN，NaN 会顺着每一步旋转污染整具骨架，最后是"角色整个消失"
    // 或"网格炸成一团"，而不是一条能读的报错。原 demo 没这道闸门，因为它没有遇到过——这不
    // 是不需要的证明，只是运气。保留 forward 的默认值时步态仍然是有限的、可看的（绕歪轴走）。

    // walkable 的真实判据是"髋 + **两条**大腿链"。原 demo 的注释写的是 "at least one usable
    // leg chain"，但代码要的是两条：只有一条腿时程序化步态摆不出交替支撑，画面比不摆更错，
    // 所以这个不一致以代码为准，注释在这里改正。
    rig.walkable = rig.hips >= 0 && rig.legUp[kRigLeft] >= 0 && rig.legUp[kRigRight] >= 0;
    return rig;
}

const char* RigProfile::SlotName(RigSlot slot) noexcept {
    switch (slot) {
        case RigSlot::Hips:         return "hips";
        case RigSlot::Spine:        return "spine";
        case RigSlot::Spine1:       return "spine1";
        case RigSlot::Chest:        return "chest";
        case RigSlot::Head:         return "head";
        case RigSlot::LegUpLeft:    return "legUp.L";
        case RigSlot::LegUpRight:   return "legUp.R";
        case RigSlot::LegLowLeft:   return "legLow.L";
        case RigSlot::LegLowRight:  return "legLow.R";
        case RigSlot::FootLeft:     return "foot.L";
        case RigSlot::FootRight:    return "foot.R";
        case RigSlot::ToeLeft:      return "toe.L";
        case RigSlot::ToeRight:     return "toe.R";
        case RigSlot::ArmUpLeft:    return "armUp.L";
        case RigSlot::ArmUpRight:   return "armUp.R";
        case RigSlot::ArmLowLeft:   return "armLow.L";
        case RigSlot::ArmLowRight:  return "armLow.R";
        case RigSlot::HandLeft:     return "hand.L";
        case RigSlot::HandRight:    return "hand.R";
        case RigSlot::ClavicleLeft: return "clavicle.L";
        case RigSlot::ClavicleRight: return "clavicle.R";
    }
    // 走不到这里：上面覆盖了每一个枚举值（而且故意不写 default，好让 -Wswitch 在新增枚举
    // 值时报警）。留着是为了让"从 int 强转来的越界值"不变成 UB，而是打出一个显眼的问号。
    return "?";
}

int RigProfile::Index(RigSlot slot) const noexcept {
    switch (slot) {
        case RigSlot::Hips:          return hips;
        case RigSlot::Spine:         return spine;
        case RigSlot::Spine1:        return spine1;
        case RigSlot::Chest:         return chest;
        case RigSlot::Head:          return head;
        case RigSlot::LegUpLeft:     return legUp[kRigLeft];
        case RigSlot::LegUpRight:    return legUp[kRigRight];
        case RigSlot::LegLowLeft:    return legLow[kRigLeft];
        case RigSlot::LegLowRight:   return legLow[kRigRight];
        case RigSlot::FootLeft:      return foot[kRigLeft];
        case RigSlot::FootRight:     return foot[kRigRight];
        case RigSlot::ToeLeft:       return toe[kRigLeft];
        case RigSlot::ToeRight:      return toe[kRigRight];
        case RigSlot::ArmUpLeft:     return armUp[kRigLeft];
        case RigSlot::ArmUpRight:    return armUp[kRigRight];
        case RigSlot::ArmLowLeft:    return armLow[kRigLeft];
        case RigSlot::ArmLowRight:   return armLow[kRigRight];
        case RigSlot::HandLeft:      return hand[kRigLeft];
        case RigSlot::HandRight:     return hand[kRigRight];
        case RigSlot::ClavicleLeft:  return clavicle[kRigLeft];
        case RigSlot::ClavicleRight: return clavicle[kRigRight];
    }
    return -1;
}

int RigProfile::BoundCount() const noexcept {
    int bound = 0;
    for (int i = 0; i < kRigSlotCount; ++i) {
        const RigSlot slot = static_cast<RigSlot>(i);
        if (slot == RigSlot::Spine1) continue;   // 辅助槽位，不在分母里
        if (Index(slot) >= 0) ++bound;
    }
    return bound;
}

std::vector<const char*> RigProfile::Missing() const {
    std::vector<const char*> missing;
    for (int i = 0; i < kRigSlotCount; ++i) {
        const RigSlot slot = static_cast<RigSlot>(i);
        if (slot == RigSlot::Spine1) continue;
        if (Index(slot) < 0) missing.push_back(SlotName(slot));
    }
    return missing;
}

std::string RigProfile::BindingReport() const {
    std::string out = std::to_string(BoundCount());
    out += '/';
    out += std::to_string(kRigRequiredSlotCount);
    out += " slots bound";

    const std::vector<const char*> missing = Missing();
    if (!missing.empty()) {
        out += "; unbound:";
        for (const char* name : missing) {
            out += ' ';
            out += name;
        }
    }
    // 辅助槽位单独提一句：它不影响"能不能走"，但会影响"裙摆/腰线的间隙探针挂在哪"，
    // 所以既不该混进 20/20 的分母，也不该彻底沉默。
    if (spine1 < 0) out += "; auxiliary spine1 unbound";
    return out;
}

} // namespace gldx
