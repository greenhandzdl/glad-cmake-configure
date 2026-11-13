module;

#include "gldx/gmf.hpp"

module gldx;

// 类型全部从模块本身拿（RigNaming.h / RigProfile.h / RigSlot.h 都在 gldx.cppm 里 export）。
// **不要**在这里文本 include RigNaming.h：那会把 Skeleton/Bone/kRigSlotCount 等已导出实体
// 再解析一遍，报成一串 redefinition（C++20 模块里，实现单元文本包含一个已 export 的头就是
// 定义两遍）。与 SkinnedMesh.cpp 不文本 include Skeleton.h 是同一条规则。

namespace gldx {

// ---------------------------------------------------------------------------
// RigNaming 的默认实现
// ---------------------------------------------------------------------------

int RigNaming::Find(const Skeleton& skeleton, RigSlot slot) const {
    // 逐个试 NamesFor()，先列的先命中。这是默认路径：一个族只实现 NamesFor() 就够了。
    for (const char* name : NamesFor(slot)) {
        const int index = skeleton.Find(name);
        if (index >= 0) return index;
    }
    return -1;
}

int RigNaming::Detect(const Skeleton& skeleton) const {
    // 数一遍能解析出多少槽位。对"没有特殊前缀"的族这已经够用；带前缀的族应当 override，
    // 否则一个纯 mixamorig: 资产在无前缀族上也会拿到虚高的分（Hips 之类恰好没前缀）。
    int hits = 0;
    for (int i = 0; i < kRigSlotCount; ++i) {
        if (Find(skeleton, static_cast<RigSlot>(i)) >= 0) ++hits;
    }
    return hits;
}

// ---------------------------------------------------------------------------
// Mixamo / Blender
// ---------------------------------------------------------------------------

const char* MixamoNaming::family() const noexcept { return "mixamo/Blender"; }

const MixamoNaming& MixamoNaming::Instance() noexcept {
    static const MixamoNaming instance;
    return instance;
}

std::span<const char* const> MixamoNaming::NamesFor(RigSlot slot) const {
    // 无 default 的 switch：加一个 RigSlot 却漏了这一族，-Wswitch 当场报。
    //
    // 这里只列**裸名**（不带 mixamorig: 前缀）。前缀由 Find() 按规律补——Mixamo 网页版
    // 给每根骨头都加前缀，把它列进表里就是 21 × 2 条字符串，改一个漏一个。
    switch (slot) {
        case RigSlot::Hips: { static constexpr const char* const k[] = {"Hips", "hips", "Pelvis", "pelvis"}; return k; }
        // spine 的列表里带 "Spine1"、spine1 的带 "Spine2"/"Chest"：这是刻意的降级链，
        // 只有两节脊柱的骨架（Spine1/Spine2 命名）用 Spine1 当腰、Spine2 当胸。
        // 代价是两节脊柱时 spine 与 spine1 指向同一根骨头，两处旋转会叠加。
        case RigSlot::Spine: { static constexpr const char* const k[] = {"Spine", "spine"}; return k; }
        case RigSlot::Spine1: { static constexpr const char* const k[] = {"Spine1", "spine_02"}; return k; }
        case RigSlot::Chest: { static constexpr const char* const k[] = {"Spine2", "Chest", "chest", "spine.003", "spine_03"}; return k; }
        case RigSlot::Head: { static constexpr const char* const k[] = {"Head", "head", "head_01"}; return k; }

        case RigSlot::LegUpLeft: { static constexpr const char* const k[] = {"LeftUpLeg", "LeftUpperLeg", "LeftThigh", "UpperLeg.L", "Thigh.L"}; return k; }
        case RigSlot::LegLowLeft: { static constexpr const char* const k[] = {"LeftLeg", "LeftLowerLeg", "LowerLeg.L", "Calf.L", "Shin.L"}; return k; }
        case RigSlot::FootLeft: { static constexpr const char* const k[] = {"LeftFoot", "Foot.L"}; return k; }
        case RigSlot::ToeLeft: { static constexpr const char* const k[] = {"LeftToeBase", "LeftToe", "Toe.L"}; return k; }
        case RigSlot::LegUpRight: { static constexpr const char* const k[] = {"RightUpLeg", "RightUpperLeg", "RightThigh", "UpperLeg.R", "Thigh.R"}; return k; }
        case RigSlot::LegLowRight: { static constexpr const char* const k[] = {"RightLeg", "RightLowerLeg", "LowerLeg.R", "Calf.R", "Shin.R"}; return k; }
        case RigSlot::FootRight: { static constexpr const char* const k[] = {"RightFoot", "Foot.R"}; return k; }
        case RigSlot::ToeRight: { static constexpr const char* const k[] = {"RightToeBase", "RightToe", "Toe.R"}; return k; }

        case RigSlot::ArmUpLeft: { static constexpr const char* const k[] = {"LeftArm", "LeftUpperArm", "UpperArm.L", "Arm.L"}; return k; }
        case RigSlot::ArmLowLeft: { static constexpr const char* const k[] = {"LeftForeArm", "LeftLowerArm", "LowerArm.L", "ForeArm.L"}; return k; }
        case RigSlot::ArmUpRight: { static constexpr const char* const k[] = {"RightArm", "RightUpperArm", "UpperArm.R", "Arm.R"}; return k; }
        case RigSlot::ArmLowRight: { static constexpr const char* const k[] = {"RightForeArm", "RightLowerArm", "LowerArm.R", "ForeArm.R"}; return k; }
        case RigSlot::HandLeft: { static constexpr const char* const k[] = {"LeftHand", "Hand.L"}; return k; }
        case RigSlot::HandRight: { static constexpr const char* const k[] = {"RightHand", "Hand.R"}; return k; }
        case RigSlot::ClavicleLeft: { static constexpr const char* const k[] = {"LeftShoulder", "LeftClavicle", "Shoulder.L", "Clavicle.L"}; return k; }
        case RigSlot::ClavicleRight: { static constexpr const char* const k[] = {"RightShoulder", "RightClavicle", "Shoulder.R", "Clavicle.R"}; return k; }
    }
    return {};
}

int MixamoNaming::Find(const Skeleton& skeleton, RigSlot slot) const {
    // 先裸名，再 mixamorig: 前缀。顺序不能反：一个 Blender 重导的无前缀资产如果先试带前缀
    // 的，会白查 21 次；更重要的是无前缀资产里两者不可能同时存在，所以顺序只影响性能。
    for (const char* name : NamesFor(slot)) {
        const int index = skeleton.Find(name);
        if (index >= 0) return index;
    }
    // 前缀是规律，所以在这里拼一次，而不是在 NamesFor() 里把每个名字写两遍。
    for (const char* name : NamesFor(slot)) {
        const int index = skeleton.Find(std::string("mixamorig:") + name);
        if (index >= 0) return index;
    }
    return -1;
}

int MixamoNaming::Detect(const Skeleton& skeleton) const {
    // 按**前缀命中率**算，不走默认的"数裸名命中"：一个纯 mixamorig: 资产在默认实现下
    // 会因为 Hips/Head 之类恰好不带前缀而拿到虚高的分，从而被误判成本族（其实是本族，
    // 但判据错了会让别族的资产也有机会高分）。这里要求至少真的看到带前缀的骨头。
    int prefixed = 0;
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
        const Bone* bone = skeleton.bone(static_cast<int>(i));
        if (bone && bone->name.rfind("mixamorig:", 0) == 0) ++prefixed;
    }
    if (prefixed == 0) {
        // 没有任何带前缀的骨头：退回默认实现，按裸名命中率算。
        return RigNaming::Detect(skeleton);
    }
    // 有前缀就说明这确实是一份 Mixamo 资产，给它一个高于任何裸名族可能拿到的分。
    return kRigSlotCount + prefixed;
}

// ---------------------------------------------------------------------------
// 3ds Max Biped
// ---------------------------------------------------------------------------

const char* BipedNaming::family() const noexcept { return "3ds Max Biped"; }

const BipedNaming& BipedNaming::Instance() noexcept {
    static const BipedNaming instance;
    return instance;
}

std::span<const char* const> BipedNaming::NamesFor(RigSlot slot) const {
    // 左右是 L/R 中缀（`Bip01 L Thigh`）而不是 Left/Right 前缀，所以每个槽位各自写全：
    // 这类命名没有能安全套用的规律，硬拼只会拼出不存在的名字。
    switch (slot) {
        case RigSlot::Hips: { static constexpr const char* const k[] = {"Bip01 Pelvis", "Bip01 Hips", "Bip01 Spine"}; return k; }
        case RigSlot::Spine: { static constexpr const char* const k[] = {"Bip01 Spine", "Bip01 Spine1"}; return k; }
        case RigSlot::Spine1: { static constexpr const char* const k[] = {"Bip01 Spine1"}; return k; }
        case RigSlot::Chest: { static constexpr const char* const k[] = {"Bip01 Spine2", "Bip01 Spine3"}; return k; }
        case RigSlot::Head: { static constexpr const char* const k[] = {"Bip01 Head"}; return k; }

        case RigSlot::LegUpLeft: { static constexpr const char* const k[] = {"Bip01 L Thigh"}; return k; }
        case RigSlot::LegLowLeft: { static constexpr const char* const k[] = {"Bip01 L Calf"}; return k; }
        case RigSlot::FootLeft: { static constexpr const char* const k[] = {"Bip01 L Foot"}; return k; }
        case RigSlot::ToeLeft: { static constexpr const char* const k[] = {"Bip01 L Toe0"}; return k; }
        case RigSlot::LegUpRight: { static constexpr const char* const k[] = {"Bip01 R Thigh"}; return k; }
        case RigSlot::LegLowRight: { static constexpr const char* const k[] = {"Bip01 R Calf"}; return k; }
        case RigSlot::FootRight: { static constexpr const char* const k[] = {"Bip01 R Foot"}; return k; }
        case RigSlot::ToeRight: { static constexpr const char* const k[] = {"Bip01 R Toe0"}; return k; }

        case RigSlot::ArmUpLeft: { static constexpr const char* const k[] = {"Bip01 L UpperArm"}; return k; }
        case RigSlot::ArmLowLeft: { static constexpr const char* const k[] = {"Bip01 L Forearm"}; return k; }
        case RigSlot::ArmUpRight: { static constexpr const char* const k[] = {"Bip01 R UpperArm"}; return k; }
        case RigSlot::ArmLowRight: { static constexpr const char* const k[] = {"Bip01 R Forearm"}; return k; }
        case RigSlot::HandLeft: { static constexpr const char* const k[] = {"Bip01 L Hand"}; return k; }
        case RigSlot::HandRight: { static constexpr const char* const k[] = {"Bip01 R Hand"}; return k; }
        case RigSlot::ClavicleLeft: { static constexpr const char* const k[] = {"Bip01 L Clavicle"}; return k; }
        case RigSlot::ClavicleRight: { static constexpr const char* const k[] = {"Bip01 R Clavicle"}; return k; }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Unreal Engine
// ---------------------------------------------------------------------------

const char* UeNaming::family() const noexcept { return "Unreal Engine"; }

const UeNaming& UeNaming::Instance() noexcept {
    static const UeNaming instance;
    return instance;
}

std::span<const char* const> UeNaming::NamesFor(RigSlot slot) const {
    // 大小写与 _l/_r 后缀都是规律的一部分，但每个槽位的词干不同，所以同样写全。
    switch (slot) {
        case RigSlot::Hips: { static constexpr const char* const k[] = {"pelvis", "hips"}; return k; }
        case RigSlot::Spine: { static constexpr const char* const k[] = {"spine_01", "spine"}; return k; }
        case RigSlot::Spine1: { static constexpr const char* const k[] = {"spine_02"}; return k; }
        case RigSlot::Chest: { static constexpr const char* const k[] = {"spine_03", "chest"}; return k; }
        case RigSlot::Head: { static constexpr const char* const k[] = {"head", "head_01"}; return k; }

        case RigSlot::LegUpLeft: { static constexpr const char* const k[] = {"thigh_l"}; return k; }
        case RigSlot::LegLowLeft: { static constexpr const char* const k[] = {"calf_l"}; return k; }
        case RigSlot::FootLeft: { static constexpr const char* const k[] = {"foot_l"}; return k; }
        case RigSlot::ToeLeft: { static constexpr const char* const k[] = {"ball_l"}; return k; }
        case RigSlot::LegUpRight: { static constexpr const char* const k[] = {"thigh_r"}; return k; }
        case RigSlot::LegLowRight: { static constexpr const char* const k[] = {"calf_r"}; return k; }
        case RigSlot::FootRight: { static constexpr const char* const k[] = {"foot_r"}; return k; }
        case RigSlot::ToeRight: { static constexpr const char* const k[] = {"ball_r"}; return k; }

        case RigSlot::ArmUpLeft: { static constexpr const char* const k[] = {"upperarm_l", "arm_l"}; return k; }
        case RigSlot::ArmLowLeft: { static constexpr const char* const k[] = {"lowerarm_l", "forearm_l"}; return k; }
        case RigSlot::ArmUpRight: { static constexpr const char* const k[] = {"upperarm_r", "arm_r"}; return k; }
        case RigSlot::ArmLowRight: { static constexpr const char* const k[] = {"lowerarm_r", "forearm_r"}; return k; }
        case RigSlot::HandLeft: { static constexpr const char* const k[] = {"hand_l"}; return k; }
        case RigSlot::HandRight: { static constexpr const char* const k[] = {"hand_r"}; return k; }
        case RigSlot::ClavicleLeft: { static constexpr const char* const k[] = {"clavicle_l", "shoulder_l"}; return k; }
        case RigSlot::ClavicleRight: { static constexpr const char* const k[] = {"clavicle_r", "shoulder_r"}; return k; }
    }
    return {};
}

} // namespace gldx
