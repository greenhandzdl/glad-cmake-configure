#ifndef ANIMRIGID_MMDNAMING_H
#define ANIMRIGID_MMDNAMING_H

/**
 * @file MmdNaming.h
 * @brief **demo 侧**的 MMD/PMX 命名族：日文骨名 -> 引擎的语义槽位。
 *
 * ## 为什么它在 demo 而不是引擎
 *
 * MMD 的骨名是 `下半身`、`ひざ.L`、`足首D.R` —— 这些是**日文自由文本**，不是
 * 标准。引擎里那一族（`MixamoNaming` / `BipedNaming` / `UeNaming`）用的是
 * 通行英文命名（`Hips` / `LeftLeg` / `LeftFoot`），那是任何资产都能对照的
 * 通用结构。
 *
 * 把日文名写进引擎有两层代价：
 *   1. 引擎要为每套"非英文命名"积累一张表，彼此还会撞车（`腰` 在别的日系工具
 *      里可能指胸）；
 *   2. 想用 MMD 资产的人会被迫接受一份他没法改的映射。
 *
 * 放在 demo 里则相反：引擎只提供 `RigNaming` 接口，认识这套日文名的责任落在
 * 使用者身上——想换个写法，改这个文件即可，不用重新编译引擎。
 *
 * ## 用法
 *
 *     gldx::RigProfile::Classify(skeleton, &animrigid::MmdNaming::Instance());
 *
 * ## 覆盖的范围
 *
 * 20 个必需槽位（见 `NamesFor()`），另外两类**可选链**：
 *
 *   · `スカート`（裙摆）：若干组 × 3 节，沿裙腰一圈辐射排布
 *   · `後ろ髪` / `横髪`（头发）：多条
 *
 * 链找不到时返回 0 —— 没有尾巴没有裙子的角色走"跳过"分支，这正是 optional
 * 的意义（`ChainSolver` 对空链直接返回）。
 */

// No gldx includes: those headers belong to the named module `gldx`, and a textual
// include from a consumer is a second declaration in the global module. The including
// .cpp does `import gldx;` FIRST, then includes this file.

#include <span>

namespace animrigid {

// ---------------------------------------------------------------------------
// Bone name tables. These are the SPECIFIC part: Japanese text, per-export.
//
// MMD naming notes, so the next person does not reverse-engineer them again:
//   · `.L` / `.R` (ASCII) carry the side. Full-width `ＩＫ` marks an IK control bone,
//     which must NOT be used as a deform joint - the body would follow the IK target
//     instead of the FK chain.
//   · `D` in `ひざD` / `足首D` means "deform" (変形用): a duplicate the skin is actually
//     weighted to.
//   · `捩` means "twist" (腕捩 = arm twist), likewise not a hinge joint.
// Every table is `static constexpr const char* const[]` at namespace scope because the
// interface returns a span - a span over a temporary would dangle.
inline constexpr const char* const kEmpty[] = {};

inline constexpr const char* const kHips[]  = {"下半身", "腰", "センター"};
inline constexpr const char* const kSpine[] = {"上半身", "上半身2"};
inline constexpr const char* const kChest[] = {"上半身2", "上半身3"};
inline constexpr const char* const kHead[]  = {"頭", "首"};
inline constexpr const char* const kNeck[]  = {"首"};

inline constexpr const char* const kLegUpL[]  = {"足.L", "左足", "脚.L"};
inline constexpr const char* const kLegUpR[]  = {"足.R", "右足", "脚.R"};
inline constexpr const char* const kLegLowL[] = {"ひざ.L", "左ひざ", "膝.L"};
inline constexpr const char* const kLegLowR[] = {"ひざ.R", "右ひざ", "膝.R"};
inline constexpr const char* const kFootL[]   = {"足首.L", "左足首", "足首D.L"};
inline constexpr const char* const kFootR[]   = {"足首.R", "右足首", "足首D.R"};
inline constexpr const char* const kToeL[]    = {"つま先.L", "左つま先"};
inline constexpr const char* const kToeR[]    = {"つま先.R", "右つま先"};
inline constexpr const char* const kArmUpL[]  = {"腕.L", "左腕", "腕捩.L"};
inline constexpr const char* const kArmUpR[]  = {"腕.R", "右腕", "腕捩.R"};
inline constexpr const char* const kArmLowL[] = {"ひじ.L", "左ひじ", "肘.L"};
inline constexpr const char* const kArmLowR[] = {"ひじ.R", "右ひじ", "肘.R"};
inline constexpr const char* const kHandL[]   = {"手首.L", "左手首"};
inline constexpr const char* const kHandR[]   = {"手首.R", "右手首"};
inline constexpr const char* const kClavL[]   = {"肩.L", "左肩"};
inline constexpr const char* const kClavR[]   = {"肩.R", "右肩"};

/**
 * MMD/PMX 命名族。这是一个**覆盖**：引擎用通行英文命名，这里把日文名映射上去。
 *
 * `family()` 返回 `"MMD/PMX (demo)"`，出现在 `--on report` 的 rig 行里，一眼能看出
 * 这份资产是被 demo 侧的覆盖解析的，而不是引擎内置族。
 */
class MmdNaming final : public gldx::RigNaming {
public:
    [[nodiscard]] const char* family() const noexcept override { return "MMD/PMX (demo)"; }

    [[nodiscard]] std::span<const char* const> NamesFor(gldx::RigSlot slot) const override;

    /**
     * 骨架里大范围存在日文骨名才算 MMD。只看一两个名字会被恰好同名的别的资产骗到
     * （`センター`、`上半身` 这种词在日系其它工具里也有）。
     */
    [[nodiscard]] int Detect(const gldx::Skeleton& skeleton) const override;

    [[nodiscard]] int FindChain(const gldx::Skeleton& skeleton, gldx::RigChainKind kind,
                                int* out, int maxOut) const override;

    [[nodiscard]] static const MmdNaming& Instance() noexcept;
};

}  // namespace animrigid

#endif  // ANIMRIGID_MMDNAMING_H
