// Order matters: the named module first, then the project header that *uses* its
// types. MmdNaming.h deliberately carries no gldx includes (those entities belong to
// module `gldx`), so it only compiles once `import gldx;` has been seen.
import gldx;

#include "MmdNaming.h"

#include <cstdio>
#include <cstring>
#include <iterator>

namespace animrigid {

using gldx::RigChainKind;
using gldx::RigSlot;

namespace {

// Every table below is `static constexpr const char* const[]` at namespace scope: the
// interface hands back a span, and returning a span over a temporary would dangle.
//
// MMD naming notes, so the next person does not have to reverse-engineer them again:
//   · `.L` / `.R` suffixes (ASCII) carry the side. The full-width `ＩＫ` marks an IK
//     control bone, which must NOT be used as a deform joint - the character would
//     follow the IK target instead of the FK chain.
//   · `D` in `ひざD` / `足首D` means "deform" (変形用): a duplicate of the joint that
//     the skin is actually weighted to, used for physics-driven deformation.
//   · `捩` means "twist" (腕捩 = arm twist), likewise not a hinge joint.
// `センター` is the **root control**, not the pelvis: in MMD it sits at the origin
// between the feet and every other bone hangs off it. Using it as Hips makes the whole
// "pelvis height" channel meaningless - measured hip height came out at 0.022 m while
// the character is 1.68 m tall, i.e. the demo was solving the ground for the feet
// instead of the body. `下半身` (lower body) is the actual pelvis.
constexpr const char* const kHips[]  = {"下半身", "腰", "センター"};
constexpr const char* const kSpine[] = {"上半身", "上半身2"};
constexpr const char* const kChest[] = {"上半身2", "上半身3"};
constexpr const char* const kHead[]  = {"頭", "首"};
constexpr const char* const kNeck[]  = {"首"};

constexpr const char* const kLegUpL[]  = {"足.L", "左足", "脚.L"};
constexpr const char* const kLegUpR[]  = {"足.R", "右足", "脚.R"};
constexpr const char* const kLegLowL[] = {"ひざ.L", "左ひざ", "膝.L"};
constexpr const char* const kLegLowR[] = {"ひざ.R", "右ひざ", "膝.R"};
constexpr const char* const kFootL[]   = {"足首.L", "左足首", "足首D.L"};
constexpr const char* const kFootR[]   = {"足首.R", "右足首", "足首D.R"};
constexpr const char* const kToeL[]    = {"つま先.L", "左つま先"};
constexpr const char* const kToeR[]    = {"つま先.R", "右つま先"};
constexpr const char* const kArmUpL[]  = {"腕.L", "左腕", "腕捩.L"};
constexpr const char* const kArmUpR[]  = {"腕.R", "右腕", "腕捩.R"};
constexpr const char* const kArmLowL[] = {"ひじ.L", "左ひじ", "肘.L"};
constexpr const char* const kArmLowR[] = {"ひじ.R", "右ひじ", "肘.R"};
constexpr const char* const kHandL[]   = {"手首.L", "左手首"};
constexpr const char* const kHandR[]   = {"手首.R", "右手首"};
constexpr const char* const kClavL[]   = {"肩.L", "左肩"};
constexpr const char* const kClavR[]   = {"肩.R", "右肩"};

constexpr const char* const kEmpty[] = {};

}  // namespace

std::span<const char* const> MmdNaming::NamesFor(RigSlot slot) const {
    switch (slot) {
        case RigSlot::Hips:        return {kHips,  std::size(kHips)};
        case RigSlot::Spine:       return {kSpine, std::size(kSpine)};
        case RigSlot::Spine1:      return {kEmpty, 0};
        case RigSlot::Chest:       return {kChest, std::size(kChest)};
        case RigSlot::Head:        return {kHead,  std::size(kHead)};
        case RigSlot::LegUpLeft:   return {kLegUpL,  std::size(kLegUpL)};
        case RigSlot::LegUpRight:  return {kLegUpR,  std::size(kLegUpR)};
        case RigSlot::LegLowLeft:  return {kLegLowL, std::size(kLegLowL)};
        case RigSlot::LegLowRight: return {kLegLowR, std::size(kLegLowR)};
        case RigSlot::FootLeft:    return {kFootL,  std::size(kFootL)};
        case RigSlot::FootRight:   return {kFootR,  std::size(kFootR)};
        case RigSlot::ToeLeft:     return {kToeL,   std::size(kToeL)};
        case RigSlot::ToeRight:    return {kToeR,   std::size(kToeR)};
        case RigSlot::ArmUpLeft:   return {kArmUpL, std::size(kArmUpL)};
        case RigSlot::ArmUpRight:  return {kArmUpR, std::size(kArmUpR)};
        case RigSlot::ArmLowLeft:  return {kArmLowL, std::size(kArmLowL)};
        case RigSlot::ArmLowRight: return {kArmLowR, std::size(kArmLowR)};
        case RigSlot::HandLeft:    return {kHandL,  std::size(kHandL)};
        case RigSlot::HandRight:   return {kHandR,  std::size(kHandR)};
        case RigSlot::ClavicleLeft:  return {kClavL, std::size(kClavL)};
        case RigSlot::ClavicleRight: return {kClavR, std::size(kClavR)};
    }
    return {kEmpty, 0};
}

int MmdNaming::Detect(const gldx::Skeleton& skeleton) const {
    // Count how many of the required slots this family can actually resolve. A plain
    // "does センター exist" test is not enough: that word shows up in other Japanese
    // toolchains too, and matching on it alone would claim rigs whose naming differs.
    int score = 0;
    for (int i = 0; i < gldx::kRigSlotCount; ++i) {
        const RigSlot s = static_cast<RigSlot>(i);
        if (s == RigSlot::Spine1) continue;
        if (Find(skeleton, s) >= 0) ++score;
    }
    return score;
}

int MmdNaming::FindChain(const gldx::Skeleton& skeleton, RigChainKind kind,
                         int* out, int maxOut) const {
    if (kind != RigChainKind::Skirt) return 0;   // this asset has no tail / ear chain

    // The skirt is **not one chain**: it is 8 radial panels of 3 joints each
    // (`スカート1-0..2`, `スカート2-0..2`, ...). Feeding only one panel to the engine
    // would animate an eighth of the hem, which reads as "the skirt is tearing".
    //
    // Flatten them in panel-major order (all of panel 1, then panel 2, ...). The
    // physics layer then treats it as several independent vertical chains, which is
    // exactly what it is. The panel count is discovered from the asset rather than
    // hardcoded, so a 6-panel or 12-panel skirt works unchanged.
    int written = 0;
    for (int panel = 1; written < maxOut; ++panel) {
        bool anyThisPanel = false;
        for (int seg = 0; seg < 3 && written < maxOut; ++seg) {
            char name[32];
            std::snprintf(name, sizeof(name), "スカート%d-%d", panel, seg);
            const int bone = skeleton.Find(name);
            if (bone >= 0) {
                out[written++] = bone;
                anyThisPanel = true;
            }
        }
        if (!anyThisPanel) break;   // no such panel -> stop scanning
    }
    return written;
}

const MmdNaming& MmdNaming::Instance() noexcept {
    static const MmdNaming kInstance;
    return kInstance;
}

}  // namespace animrigid
