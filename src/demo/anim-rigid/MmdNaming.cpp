// Module first, then the project header that uses its types: MmdNaming.h carries no
// gldx includes (those entities belong to module `gldx`), so it only compiles once
// `import gldx;` has been seen.
import gldx;

#include "MmdNaming.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <span>

namespace animrigid {

namespace {

/**
 * Render an int as the FULL-WIDTH digit(s) MMD uses in bone names.
 *
 * This asset names its skirt bones `スカート４-１` with U+FF10..U+FF19, not ASCII
 * '0'..'9'. Searching for "スカート4-1" finds nothing and every chain silently comes
 * back empty - the solver then does nothing and the skirt looks uninterpolated, which
 * reads as "the solver is broken" rather than "the name did not match".
 * Full-width digits run FF10..FF19, i.e. a constant +0xFEE0 offset.
 */
void AppendFullWidthDigits(char* buf, std::size_t cap, std::size_t& pos, int value) {
    char ascii[8];
    const int n = std::snprintf(ascii, sizeof(ascii), "%d", value);
    for (int i = 0; i < n && pos + 4 < cap; ++i) {
        // Multi-byte UTF-8: FF10 encodes as EF BC 90, so the offset lands in the low
        // byte of a 3-byte sequence.
        const unsigned cp = 0xFF10u + static_cast<unsigned>(ascii[i] - '0');
        buf[pos++] = static_cast<char>(0xE0u | (cp >> 12));
        buf[pos++] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        buf[pos++] = static_cast<char>(0x80u | (cp & 0x3Fu));
    }
    buf[pos] = '\0';
}

}  // namespace

std::span<const char* const> MmdNaming::NamesFor(gldx::RigSlot slot) const {
    switch (slot) {
        case gldx::RigSlot::Hips:        return {kHips,  std::size(kHips)};
        case gldx::RigSlot::Spine:       return {kSpine, std::size(kSpine)};
        case gldx::RigSlot::Spine1:      return {};
        case gldx::RigSlot::Chest:       return {kChest, std::size(kChest)};
        case gldx::RigSlot::Head:        return {kHead,  std::size(kHead)};
        case gldx::RigSlot::LegUpLeft:   return {kLegUpL,  std::size(kLegUpL)};
        case gldx::RigSlot::LegUpRight:  return {kLegUpR,  std::size(kLegUpR)};
        case gldx::RigSlot::LegLowLeft:  return {kLegLowL, std::size(kLegLowL)};
        case gldx::RigSlot::LegLowRight: return {kLegLowR, std::size(kLegLowR)};
        case gldx::RigSlot::FootLeft:    return {kFootL,  std::size(kFootL)};
        case gldx::RigSlot::FootRight:   return {kFootR,  std::size(kFootR)};
        case gldx::RigSlot::ToeLeft:     return {kToeL,   std::size(kToeL)};
        case gldx::RigSlot::ToeRight:    return {kToeR,   std::size(kToeR)};
        case gldx::RigSlot::ArmUpLeft:   return {kArmUpL, std::size(kArmUpL)};
        case gldx::RigSlot::ArmUpRight:  return {kArmUpR, std::size(kArmUpR)};
        case gldx::RigSlot::ArmLowLeft:  return {kArmLowL, std::size(kArmLowL)};
        case gldx::RigSlot::ArmLowRight: return {kArmLowR, std::size(kArmLowR)};
        case gldx::RigSlot::HandLeft:    return {kHandL,  std::size(kHandL)};
        case gldx::RigSlot::HandRight:   return {kHandR,  std::size(kHandR)};
        case gldx::RigSlot::ClavicleLeft:  return {kClavL, std::size(kClavL)};
        case gldx::RigSlot::ClavicleRight: return {kClavR, std::size(kClavR)};
    }
    return {};
}

int MmdNaming::Detect(const gldx::Skeleton& skeleton) const {
    // Count how many required slots this family can resolve. A single "does 下半身
    // exist" test is not enough: that word appears in other Japanese toolchains, and
    // matching on it alone would claim rigs whose naming differs elsewhere.
    int score = 0;
    for (int i = 0; i < gldx::kRigSlotCount; ++i) {
        const gldx::RigSlot s = static_cast<gldx::RigSlot>(i);
        if (s == gldx::RigSlot::Spine1) continue;
        if (Find(skeleton, s) >= 0) ++score;
    }
    return score;
}

int MmdNaming::FindChain(const gldx::Skeleton& skeleton, gldx::RigChainKind kind,
                         int* out, int maxOut) const {
    if (kind == gldx::RigChainKind::Hair) {
        // Hair is many short strands, and **the names are not what the .blend shows**.
        //
        // Blender's MMD importer keeps the PMX panel names verbatim (`後ろ髪２-１.L`),
        // but assimp hands this engine the MMD-native form instead:
        //     右後ろ髪６-１ / 左後ろ髪６-１      (side PREFIX, full-width digits)
        //     左横髪１ / 右横髪１ / 前髪２       (side prefix, no segment suffix)
        // Building `後ろ髪{n}-{seg}` therefore matched exactly one bone per group and the
        // chain came back as `count=4` - non-zero, which reads as "hair is solved" while
        // 52 of the 56 strand bones sat welded in bind pose. Printing the skeleton's own
        // names (`GLDX_BONES=後ろ髪`) is what settled it; guessing at the pattern twice
        // had produced two different wrong answers.
        //
        // So: ENUMERATE the skeleton and match by shape, instead of synthesising names
        // and hoping. Order matters for the spring solver (it wants base -> tip along a
        // strand), and the skeleton already stores those strands contiguously, so the
        // scan below emits them in skeleton order.
        int written = 0;
        for (std::size_t i = 0; i < skeleton.bones().size() && written < maxOut; ++i) {
            const std::string& nm = skeleton.bones()[i].name;
            // Any of the long-hair groups, with or without a side prefix. `前髪`
            // (fringe) is included: it is short but it does move, and leaving it out
            // is the same mistake in miniature.
            const bool isHair = nm.find("後ろ髪") != std::string::npos ||
                                nm.find("横髪") != std::string::npos ||
                                nm.find("前髪") != std::string::npos ||
                                nm.find("まとめ髪") != std::string::npos;
            if (isHair) out[written++] = static_cast<int>(i);
        }
        return written;
    }
    // The tail is ONE chain, not a set of panels: `しっぽ１` .. `しっぽ１２` (plus a
    // base joint `しっぽ支`). It is a single articulated rope hanging off the hips, so
    // the order is simply 1, 2, 3, ... and the solver wants them base-to-tip.
    //
    // Returning 0 here - which this used to do, with a comment claiming the asset had
    // no tail - is NOT a neutral no-op. An unsolved chain does not disappear; it stays
    // welded in bind pose. On a tail that reads as a thick rigid tube folded at the
    // hip, and nothing in the picture points at this function. The report line says
    // `kind=0 count=0`, which looks like "the asset has no tail" rather than "the
    // lookup was never asked".
    //
    // MMD tails appear in two shapes: numbered `しっぽ１`, or per-segment suffixed
    // `しっぽ１-１`. Try plain-numbered first, then the dashed reading, and stop at the
    // first gap - a hole mid-chain means this is a different chain.
    if (kind == gldx::RigChainKind::Tail) {
        int written = 0;
        for (int seg = 1; seg <= maxOut; ++seg) {
            char name[64];
            std::size_t pos = 0;
            const char* prefix = "しっぽ";
            for (const char* p = prefix; *p && pos + 1 < sizeof(name); ++p) name[pos++] = *p;
            name[pos] = '\0';
            AppendFullWidthDigits(name, sizeof(name), pos, seg);
            int bone = skeleton.Find(name);
            if (bone < 0) {
                std::size_t dashed = pos;
                if (dashed + 1 < sizeof(name)) name[dashed++] = '-';
                name[dashed] = '\0';
                AppendFullWidthDigits(name, sizeof(name), dashed, 1);
                bone = skeleton.Find(name);
            }
            if (bone < 0) break;
            out[written++] = bone;
        }
        return written;
    }

    if (kind != gldx::RigChainKind::Skirt) return 0;   // no ear chain here

    // The skirt is **not one chain**: it is N radial panels of 3 joints each
    // (`スカート1-0..2`, `スカート2-0..2`, ...). Feeding only one panel would animate
    // an eighth of the hem, which reads as "the skirt is tearing". Flatten in
    // panel-major order; the panel count is discovered, not hardcoded.
    int written = 0;
    for (int panel = 1; written < maxOut; ++panel) {
        bool anyThisPanel = false;
        for (int seg = 0; seg < 3 && written < maxOut; ++seg) {
            char name[64];
            std::size_t pos = 0;
            const char* prefix = "スカート";
            for (const char* p = prefix; *p && pos + 1 < sizeof(name); ++p) name[pos++] = *p;
            name[pos] = '\0';
            AppendFullWidthDigits(name, sizeof(name), pos, panel);
            if (pos + 1 < sizeof(name)) name[pos++] = '-';
            name[pos] = '\0';
            AppendFullWidthDigits(name, sizeof(name), pos, seg);
            const int bone = skeleton.Find(name);
            if (bone >= 0) { out[written++] = bone; anyThisPanel = true; }
        }
        if (!anyThisPanel) break;
    }
    return written;
}

const MmdNaming& MmdNaming::Instance() noexcept {
    static const MmdNaming kInstance;
    return kInstance;
}

}  // namespace animrigid
