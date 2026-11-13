#ifndef GLDX_ANIM_RIGNAMING_H
#define GLDX_ANIM_RIGNAMING_H

/**
 * @file RigNaming.h
 * @brief 骨骼命名族的抽象接口：一个导出器/建模软件怎么叫"髋"、"膝"、"腕"。
 *
 * 这是 RigProfile 的可替换的一半。骨骼名字是导出器/建模软件的自由文本，不是标准；同一份
 * 资产在 Blender 里重导一次就可能从 `LeftUpLeg` 变成 `UpperLeg.L`，3ds Max Biped 是
 * `Bip01 L Thigh`，UE 是 `thigh_l`，混元（Hunyuan）是另一套。所以"名字→语义"这件事
 * 必须是一个**可以被实现、被替换、被新增的接口**，而不是一张越写越长的字符串表。
 *
 * 分层的理由（也是加一个族时该改哪里）：
 *   - `RigNaming`（本文件）  只回答"这个族里髋叫什么"。纯名字，不知道 Skeleton 长什么样，
 *                            也因此不需要 Skeleton 就能测（拿一个假 Skeleton 或直接跑字符串）。
 *   - `RigProfile`           拿一个 `RigNaming` 去**查**一具真实骨架，量出三轴，装成可读的槽位。
 *
 * ## 为什么是抽象类而不是"再往表里加几行"
 *
 * 别名表能表达"同一个语义有多种拼法"，但表达不了三件真事，而这三件事在加混元时全都会遇到：
 *   1. **同一个名字在不同族里指不同的骨头。** `Spine1` 在 Blender 族里是"第二节腰椎"，
 *      在 3ds Max Biped 里却是"胸"。一张合并表只能挑一个，另一个族就被悄悄绑错。
 *   2. **一个族的命名是有规律的，不该穷举。** Mixamo 给每根骨头加 `mixamorig:` 前缀，
 *      混元同理——这类规律应该是 `override` 里的一行 `prefix + base`，不是 24 条重复字符串。
 *   3. **有的族需要看骨架才认得出来。** 光靠名字表无法判断"这份资产到底是哪一族"，
 *      而 `Detect()` 让每个族自己回答"这具骨架是不是我的"，于是新增一族不需要改任何已有代码。
 *
 * ## 默认实现是刻意的
 *
 * `Find()` 的默认实现走 `NamesFor(slot)`，所以一个族**只需要**实现 `NamesFor()`；只有命名有
 * 规律（前缀/后缀）或需要额外判据时才 override `Find()`。这不是"为了少写代码"，而是让
 * 最常见的那个扩展点（多几个别名）保持一行一个槽位的样子，从而不会有人为了加一个别名
 * 去 override 整个 `Find()` 并顺手抄错别的槽位。
 *
 * ## 返回值约定
 *
 * 所有 `Find*` 返回骨骼下标，**查不到返回 -1**，不抛异常、不打印。静默是刻意的（见
 * RigProfile.h 里那段"静默降级比崩溃难查"），补偿办法是 RigProfile 的
 * `Missing()` / `BindingReport()` 在加载期点名。实现者必须遵守这条：把"没找到"编成 0 或
 * 抛异常都会让上层那条断言失效。
 *
 * 本文件纯 CPU、不含 GL、不含 Assimp，可在工作线程使用。
 */

#include <span>

#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

/**
 * 一个骨骼命名族。派生类实现 `NamesFor()`（可选 override `Find()` 与 `Detect()`）。
 *
 * 生命周期：`RigProfile::Classify` 只在调用期间借用它，不持有。所以实现可以是无状态的
 * 单例（`MixamoNaming::Instance()`），也可以是一个带前缀字段的临时对象。
 */
class RigNaming {
public:
    virtual ~RigNaming() = default;

    // 族名，写进诊断（"mixamo/Blender"、"3ds Max Biped"…）。返回静态字面量，不分配。
    [[nodiscard]] virtual const char* family() const noexcept = 0;

    /**
     * 这个槽位在本族里可能叫哪些名字，**按优先级排列**：先列本族自己的写法，再列等价写法。
     * 同一具骨架里真出现两个候选（例如同时有 `Spine` 和 `Spine1`）时，先列的那个胜出。
     *
     * 返回空 span 表示"本族对这个槽位没有说法"，`Find()` 据此返回 -1。
     *
     * 返回 span 而不是 `std::initializer_list<const char*>`：从花括号初始化列表返回
     * initializer_list 会返回**指向已销毁局部数组**的指针（编译器 -Wreturn-stack-address
     * 会报，但如果不看警告就会真的读坏内存）。实现方必须返回指向 **static** 存储的 span，
     * 见各族的 `NamesFor()` 里那种 `static constexpr const char* const[]` 写法。
     */
    [[nodiscard]] virtual std::span<const char* const> NamesFor(RigSlot slot) const = 0;

    /**
     * 在骨架上解析一个槽位。默认实现逐个试 `NamesFor()`。
     *
     * override 它的正当理由只有两个：命名有规律（给每根骨头加前缀，如 Mixamo 的
     * `mixamorig:`），或者解析需要额外判据（层级、左右顺序）。**只多几个别名时不要
     * override 这个函数**——加 `NamesFor()` 的对应分支，否则容易顺手抄错别的槽位。
     */
    [[nodiscard]] virtual int Find(const Skeleton& skeleton, RigSlot slot) const;

    /**
     * 这具骨架有多像本族（0 = 不像，越大越像）。`RigProfile::Classify` 拿它挑默认族。
     *
     * 默认实现数一遍 `NamesFor()` 里能解析出多少个槽位——这对"没有特殊前缀"的族就够用了。
     * 带前缀的族应当 override，按前缀命中率算，否则一个纯 Mixamo 资产在无前缀族上也会拿到
     * 一个虚高的分（`Hips` 之类恰好不带前缀的名字会命中）。
     *
     * 只用于挑选，不用于判断"能不能用"：挑错了的后果是槽位大面积 -1，会被 BindingReport
     * 点名，而不是静默绑错。
     */
    [[nodiscard]] virtual int Detect(const Skeleton& skeleton) const;
};

/**
 * Mixamo / Blender 族：`Hips`/`LeftUpLeg`/`LeftForeArm`…，以及 Mixamo 网页版给**每根**
 * 骨头加的 `mixamorig:` 前缀。
 *
 * 前缀是按规律处理的（`Find()` 里先试裸名再试 `mixamorig:` + 裸名），不是把每根骨头的
 * 两种写法都列进表里——那样 21 个槽位就是 42 条字符串，改一个漏一个。
 */
class MixamoNaming final : public RigNaming {
public:
    [[nodiscard]] const char* family() const noexcept override;
    [[nodiscard]] std::span<const char* const> NamesFor(RigSlot slot) const override;
    [[nodiscard]] int Find(const Skeleton& skeleton, RigSlot slot) const override;
    [[nodiscard]] int Detect(const Skeleton& skeleton) const override;

    [[nodiscard]] static const MixamoNaming& Instance() noexcept;
};

/**
 * 3ds Max Biped 族：`Bip01 Pelvis` / `Bip01 L Thigh` / `Bip01 R Calf`…
 *
 * 注意左右是 `L`/`R` 中缀而不是 `Left`/`Right` 前缀，所以 `NamesFor()` 里每个槽位各自
 * 写全——这类命名没有能安全套用的规律，硬套只会拼出不存在的名字。
 */
class BipedNaming final : public RigNaming {
public:
    [[nodiscard]] const char* family() const noexcept override;
    [[nodiscard]] std::span<const char* const> NamesFor(RigSlot slot) const override;

    [[nodiscard]] static const BipedNaming& Instance() noexcept;
};

/**
 * Unreal Engine 族：`pelvis` / `thigh_l` / `calf_r` / `spine_01`…
 *
 * 大小写与下划线后缀都是规律的一部分（`_l`/`_r`），所以同样是每槽位写全。
 */
class UeNaming final : public RigNaming {
public:
    [[nodiscard]] const char* family() const noexcept override;
    [[nodiscard]] std::span<const char* const> NamesFor(RigSlot slot) const override;

    [[nodiscard]] static const UeNaming& Instance() noexcept;
};

} // namespace gldx

#endif // GLDX_ANIM_RIGNAMING_H
