module;

#include "gldx/gmf.hpp"

#include <cmath>
#include <initializer_list>

module gldx;

// RigNaming 的类型全部从模块本身拿（RigProfile.h 已经文本 include 了 RigNaming.h，
// 而两者都在 gldx.cppm 里 export）。这里**不要**再文本 include RigNaming.h：那会把
// Skeleton/Bone/kRigSlotCount 等已导出实体再解析一遍，报成一串 redefinition——
// 与 SkinnedMesh.cpp 不文本 include Skeleton.h 是同一条规则。

namespace gldx {

namespace {

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

RigProfile RigProfile::Classify(const Skeleton& skeleton, const RigNaming* naming) {
    // 没指定族就自动挑一个。挑错了不会静默绑错——槽位会大面积 -1，BindingReport 会点名。
    const RigNaming& family = naming ? *naming : DetectBestNaming(skeleton);

    RigProfile rig;
    rig.namingFamily_ = family.family();

    // 槽位解析全部委托给命名的族。这里不再有任何骨名：加一族（例如混元）只要新增一个
    // RigNaming 派生类并注册进 DetectBestNaming 的内置列表，本函数一行都不用改。
    //
    // 曾经的写法是一张全局别名表，读起来没问题但表达不了两件事：同一个名字在不同族里
    // 指不同的骨头（Spine1 在 Blender 是腰椎、在 Biped 是胸），以及"给每根骨头加前缀"
    // 这种规律（Mixamo 的 mixamorig:）。前者会让合并表必然绑错一族，后者会逼出 21 条
    // 重复字符串。
    auto slot = [&](RigSlot s) { return family.Find(skeleton, s); };

    rig.hips   = slot(RigSlot::Hips);
    rig.spine  = slot(RigSlot::Spine);
    rig.spine1 = slot(RigSlot::Spine1);
    rig.chest  = slot(RigSlot::Chest);
    rig.head   = slot(RigSlot::Head);

    rig.legUp[kRigLeft]     = slot(RigSlot::LegUpLeft);
    rig.legLow[kRigLeft]    = slot(RigSlot::LegLowLeft);
    rig.foot[kRigLeft]      = slot(RigSlot::FootLeft);
    rig.toe[kRigLeft]       = slot(RigSlot::ToeLeft);
    rig.legUp[kRigRight]    = slot(RigSlot::LegUpRight);
    rig.legLow[kRigRight]   = slot(RigSlot::LegLowRight);
    rig.foot[kRigRight]     = slot(RigSlot::FootRight);
    rig.toe[kRigRight]      = slot(RigSlot::ToeRight);

    rig.armUp[kRigLeft]     = slot(RigSlot::ArmUpLeft);
    rig.armLow[kRigLeft]    = slot(RigSlot::ArmLowLeft);
    rig.armUp[kRigRight]    = slot(RigSlot::ArmUpRight);
    rig.armLow[kRigRight]   = slot(RigSlot::ArmLowRight);
    rig.hand[kRigLeft]      = slot(RigSlot::HandLeft);
    rig.hand[kRigRight]     = slot(RigSlot::HandRight);
    rig.clavicle[kRigLeft]  = slot(RigSlot::ClavicleLeft);
    rig.clavicle[kRigRight] = slot(RigSlot::ClavicleRight);

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

const RigNaming& RigProfile::DetectBestNaming(const Skeleton& skeleton) {
    // 内置族的注册点：**加一族只改这一行**。顺序是平手时的裁决顺序，固定下来是为了让
    // 同一份资产在两次运行里挑到同一族——否则槽位解析结果会随无关改动漂移，而"报告变了"
    // 看起来像资产变了。
    //
    // 新增一族（例如混元）的步骤：写一个 RigNaming 派生类 → 加进这个数组。Classify() 与
    // 所有调用方都不用改。
    const RigNaming* const builtin[] = {
        &MixamoNaming::Instance(),
        &BipedNaming::Instance(),
        &UeNaming::Instance(),
    };

    const RigNaming* best = builtin[0];
    int bestScore = best->Detect(skeleton);
    for (std::size_t i = 1; i < std::size(builtin); ++i) {
        const int score = builtin[i]->Detect(skeleton);
        // 严格大于：平手时保留先注册的那一族，保证结果稳定。
        if (score > bestScore) {
            bestScore = score;
            best = builtin[i];
        }
    }
    return *best;
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
