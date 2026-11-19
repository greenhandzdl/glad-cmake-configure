// Module first, then the project header that uses its types: MmdNaming.h carries no
// gldx includes (those entities belong to module `gldx`), so it only compiles once
// `import gldx;` has been seen.
import gldx;

#include "MmdNaming.h"

#include <cstdio>
#include <cstring>
#include <iterator>

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
        case gldx::RigSlot::Spine1:      return {kEmpty, 0};
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
    return {kEmpty, 0};
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
        // Hair is per-strand chains named 後ろ髪<n>-<seg> / 横髪<n>-<seg>. Same
        // panel-major flattening as the skirt.
        int written = 0;
        for (const char* group : {"後ろ髪", "横髪"}) {
            for (int strand = 1; written < maxOut; ++strand) {
                bool any = false;
                for (int seg = 1; seg <= 6 && written < maxOut; ++seg) {
                    char name[64];
                    std::size_t pos = 0;
                    for (const char* p = group; *p && pos + 1 < sizeof(name); ++p) name[pos++] = *p;
                    name[pos] = '\0';
                    AppendFullWidthDigits(name, sizeof(name), pos, strand);
                    if (pos + 1 < sizeof(name)) name[pos++] = '-';
                    name[pos] = '\0';
                    AppendFullWidthDigits(name, sizeof(name), pos, seg);
                    int b = skeleton.Find(name);
                    if (b < 0) {
                        char sided[80];
                        std::snprintf(sided, sizeof(sided), "%s.L", name);
                        b = skeleton.Find(sided);
                        if (b < 0) {
                            std::snprintf(sided, sizeof(sided), "%s.R", name);
                            b = skeleton.Find(sided);
                        }
                    }
                    if (b >= 0) { out[written++] = b; any = true; }
                }
                if (!any) break;
            }
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
