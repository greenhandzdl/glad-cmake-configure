#ifndef ANIMRIGID_MMDNAMING_H
#define ANIMRIGID_MMDNAMING_H

/**
 * @file MmdNaming.h
 * @brief **demo 侧**的 MMD/PMX 命名族：日文骨名 -> 引擎的语义槽位。
 *
 * 这个文件是「引擎假设规范命名、demo 处理特异命名」那条分工的落点，也是唯一一个
 * 认识具体资产骨名的地方。引擎（`gldx::RigNaming`）只提供接口，不认识 `腕捩`、
 * `ひざD` 这类名字。
 *
 * ## 为什么要放在 demo 而不是引擎
 *
 * MMD 的骨名是**建模者自由文本**，不是标准：`ひざ` 和 `膝` 都有人用，`足首D` 的
 * `D` 表示"变形用"，不是所有人都加。把这类名字写进引擎，等于把一份特定资产的
 * 惯例当成了通用知识，而且每来一个新资产就要往引擎里加一张表。放在 demo 里，
 * 引擎的 `RigNaming` 列表保持"通用族"（Mixamo/Blender、3ds Max Biped、UE），
 * 特异写法留在它该在的地方。
 *
 * ## 这个族覆盖的东西
 *
 * 20 个必需槽位里，MMD 用一套完全不同的命名（见 NamesFor 的表）。此外它还有引擎
 * 必需槽位之外的**可选链**：
 *
 *   · `スカート`（裙摆）：8 组 × 3 节，沿裙腰一圈辐射排布
 *   · `後ろ髪` / `前髪` / `横髪`（头发）：多条
 *   · 尾巴：本资产没有
 *
 * 这些通过 `FindChain()` 报给引擎；报了之后解算才会动它们，没报就完全跳过——
 * 这正是 optional 的意义。
 */

// NOTE: no `#include "gldx/..."` here. Those headers are attached to the named module
// `gldx`, so a textual include from a consumerTU is a second declaration in the global
// module - the compiler rejects it with "declaration of 'X' in the global module
// follows declaration in module gldx". The consumer includes this header *after*
// `import gldx;` and the types are already visible.

#include <span>
#include <string>

namespace animrigid {

/**
 * MMD/PMX 标准骨架 + 这个资产的特异写法。
 *
 * `family()` 返回 `"MMD/PMX"`，会出现在 `--on report` 的 rig 行里，便于确认
 * 这份资产到底被当成哪一族解析了。
 */
class MmdNaming final : public gldx::RigNaming {
public:
    [[nodiscard]] const char* family() const noexcept override { return "MMD/PMX"; }

    [[nodiscard]] std::span<const char* const> NamesFor(gldx::RigSlot slot) const override;

    /**
     * 骨架里大范围存在日文骨名才算 MMD。只看两三个名字会被恰好同名的别的资产骗到
     * （`センター`、`上半身` 这种词在日系其它工具里也有）。
     */
    [[nodiscard]] int Detect(const gldx::Skeleton& skeleton) const override;

    [[nodiscard]] int FindChain(const gldx::Skeleton& skeleton, gldx::RigChainKind kind,
                                int* out, int maxOut) const override;

    [[nodiscard]] static const MmdNaming& Instance() noexcept;
};

}  // namespace animrigid

#endif  // ANIMRIGID_MMDNAMING_H
