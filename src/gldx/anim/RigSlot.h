#ifndef GLDX_ANIM_RIGSLOT_H
#define GLDX_ANIM_RIGSLOT_H

/**
 * @file RigSlot.h
 * @brief 骨骼的**语义**槽位：一份蒙皮代码真正关心的那些关节，与它叫什么名字无关。
 *
 * 单独成文件是为了打断循环依赖：`RigNaming.h`（"一个族怎么叫这些槽位"）需要本文件的
 * `RigSlot`，而 `RigProfile.h`（"一具骨架的槽位解析结果"）需要 `RigNaming`。把枚举放在
 * 两者都能包含的最底层，接口与实现就先解耦了，谁都不必反向包含对方。
 *
 * 纯枚举 + 两个整型常量，不含 GL、不含 glm、不依赖 Skeleton。
 */

namespace gldx {

// 侧别下标。0 = 左，1 = 右；左右以**角色自己**的左右为准（不是屏幕左右），
// 这也正是 RigProfile 里 side 轴 = 左大腿 − 右大腿 的定义方向。
inline constexpr int kRigLeft  = 0;
inline constexpr int kRigRight = 1;
inline constexpr int kRigSides = 2;

/**
 * 语义槽位的枚举形式。存在的理由只有一个：`Missing()` / `BindingReport()` 要能**遍历**槽位，
 * 而结构体里的具名成员（hips、legUp[2]……）没法遍历。具名成员保留给调用方读写用，枚举给
 * 诊断用，两者通过 `RigProfile::Index()` / `SlotName()` 连接。
 *
 * 两个 switch（Index 与 SlotName）都覆盖全部枚举值且没有 default：新增一个枚举值而忘了在
 * 其中一个里处理，`-Wall` 的 `-Wswitch` 会当场报出来。这就是不写 default 的目的，别"补全"。
 *
 * 同理，`RigNaming` 的每个族的 `NamesFor()` 也是无 default 的 switch——加一个槽位却漏了
 * 某个族，编译期就报，而不是运行到混元资产上才发现那个槽位永远是 -1。
 */
enum class RigSlot : int {
    Hips,
    Spine,
    Spine1,        // 辅助槽位（躯干中段），不计入 20/20，见 RigProfile.h 的 kRigRequiredSlotCount
    Chest,
    Head,
    LegUpLeft,
    LegUpRight,
    LegLowLeft,
    LegLowRight,
    FootLeft,
    FootRight,
    ToeLeft,
    ToeRight,
    ArmUpLeft,
    ArmUpRight,
    ArmLowLeft,
    ArmLowRight,
    HandLeft,
    HandRight,
    ClavicleLeft,
    ClavicleRight,
};

// 枚举总数（含辅助槽位）。BoundCount()/Missing() 就是按 [0, kRigSlotCount) 遍历的。
// 用 static_assert 把它钉在枚举上，而不是靠"数一遍写个 21"：`static_cast<RigSlot>(i)` 的
// 遍历写法要求这两个数永远一致，写错时后果是**遍历漏掉最后一个槽位**（clavicle.R 悄悄从
// 20/20 的分母里消失，报表还显示"全绑上了"），而这种错不会有任何编译期提示。
inline constexpr int kRigSlotCount = static_cast<int>(RigSlot::ClavicleRight) + 1;
static_assert(kRigSlotCount == 21, "RigSlot 多了/少了成员：同步更新 kRigSlotCount 与遍历");

// "20/20 slots bound" 里的那个 20：步态/蒙皮真正会用到的槽位。spine1 是辅助槽位（躯干中段，
// 给裙摆/腰线的间隙探针当锚点用，可以拿来量也能拿来轻微旋转），缺了不影响走路，所以它不在
// 断言的分母里——把它算进去会让"骨架只有两节脊柱"这种完全正常的情况永远报 19/20。
inline constexpr int kRigRequiredSlotCount = 20;

} // namespace gldx

#endif // GLDX_ANIM_RIGSLOT_H
