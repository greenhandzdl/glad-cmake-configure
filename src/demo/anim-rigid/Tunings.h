#ifndef ANIMRIGID_TUNINGS_H
#define ANIMRIGID_TUNINGS_H

/**
 * @file Tunings.h
 * @brief **按资产**的动作参数集：同一套 pose source，不同的数值。
 *
 * ## 为什么必须分开
 *
 * `ProceduralWalk` 这些类的**曲线形状**是通用人形知识（支撑相 60%、髋膝踝怎么
 * 配），但**数值**是在某一具骨架上试出来的。换一具骨架，即使关节语义对得上、
 * 身高相近，数值也不一定能直接搬：
 *
 *   · **权重质量**：蒙皮权重刷得规范的资产，关节角可以取大一点也不会撕开；
 *     权重粗糙的资产（下摆顶点被单根大腿骨全权重带走）角度一大就露出刚体三角，
 *     必须把摆幅收小。hunyuan 那份就是后者。
 *   · **骨骼比例**：大腿/小腿长度比不同，同样的髋角摆出来的步幅不同，脚会打滑。
 *   · **辅助骨**：MMD 骨架有 `足D`/`ひざD` 这类形变副本骨，它们的权重分担方式
 *     与 hunyuan 的单链结构不一样。
 *
 * 所以 demo 里每个命名族配一份 `PoseTunings`，而不是全局共用一份默认值。
 * 加一个新资产 = 加一份 tuning + 在 `TuningsFor()` 里加一行。
 *
 * ## 怎么读这个结构
 *
 * 每个字段就是一个 pose source 的整份 tuning 结构体，值取自引擎的默认值再做
 * 该资产的修正。留空的字段表示"用引擎默认"。
 */

// No `#include "gldx/..."`: those headers are attached to the named module `gldx`, and
// a textual include from a consumer is a second declaration in the global module. The
// including TU must `import gldx;` first; the types are then already visible.

namespace animrigid {

/**
 * Which asset family does this, for the purpose of choosing numbers. Kept separate
 * from the demo's own `NamingChannel` so Tunings.h does not have to include main.cpp.
 */
enum class NamingChannelForTunings { Rigid, Coarse };

/**
 * 一份资产的全部动作参数。
 *
 * 默认值 = 引擎里的值 = **在 MMD/PMX 这具骨架（240 骨、权重刷得规范）上量出来的**。
 * 也就是说引擎持有的就是"标准模板"，它不是中立的占位值。
 *
 * 其他资产的做法是**继承再覆盖**（见下面的 CoarseWalkTuning）：派生 struct 从引擎
 * 基类继承，只改真正不同的那几个字段。这样"这份资产改了哪些数"在代码里是可读的，
 * 而复制一整份 struct 会让读者看不出哪一项是有意为之、哪一项是抄漏了。
 */
struct PoseTunings {
    gldx::WalkTuning   walk{};
    gldx::IdleTuning   idle{};
    gldx::RunTuning    run{};
    gldx::CrouchTuning crouch{};
    gldx::AttackTuning attack{};
    gldx::JumpTuning   jump{};
    gldx::SitTuning    sit{};
    gldx::SwimTuning   swim{};
};

/**
 * 权重粗糙 + 骨骼没绑好的资产（hunyuan）：从引擎的 MMD 模板**继承**，只覆盖幅度。
 *
 * 为什么必须收小：那具骨架的下摆顶点被单根大腿骨全权重带走，髋一摆就把它们当作
 * 刚体三角拖出裙锥，画面读作"裙子裂开"；膝屈大了小腿离开大腿、没有过渡权重就会
 * 折出棱角；手臂摆动则让袖子和躯干互穿。这三处都不是"动作错了"，是"这具骨架承受
 * 不了这么大的幅度"。
 *
 * 收的是**幅度**不是形状：支撑相比例、相位关系、髋膝踝的配比全部原样继承，所以
 * 两者读起来是同一个动作，只是小的那个更保守。
 *
 * `armDown` 是这里唯一的"不是幅度"的覆盖，因为它是**几何量**而不是风格量：引擎那个
 * 0.790 rad 是从 MMD 的 A-pose bind 算出来的（上臂已在水平下 44.8°，只剩 45.2°），
 * 而这具骨架的 bind 接近 T-pose（上臂 up -1.49 / side +9.71，只在水平下 8.7°），
 * 到垂直需要 81°=1.42 rad。同一个动作参数套在绑法不同的骨架上，就是这一步先错。
 */
struct CoarseWalkTuning : gldx::WalkTuning {
    CoarseWalkTuning() {
        hipSwing      = 0.22f;   // 基类 0.33
        kneeSwingFlex = 0.52f;   // 基类 0.72
        armSwing      = 0.26f;   // 基类 0.38
        hipYaw        = 0.06f;   // 基类 0.10 - 骨盆旋转会剪切下摆
        chestYaw      = 0.09f;   // 基类 0.15
        armDown       = 1.40f;   // 基类 0.790（MMD 的 A-pose）
    }
};

struct CoarseRunTuning : gldx::RunTuning {
    CoarseRunTuning() {
        hipSwing      = 0.30f;
        kneeSwingFlex = 0.62f;
        armDown       = 1.40f;   // 基类 0.790
    }
};

struct CoarseIdleTuning : gldx::IdleTuning {
    CoarseIdleTuning() {
        breathChest   = 0.010f;  // 基类 0.018 - 躯干权重成块
        swayAmplitude = 0.007f;  // 基类 0.010
        armDown       = 1.40f;   // 基类 0.790
    }
};

/**
 * 权重刷得规范的骨架（MMD/PMX）：引擎默认值就是在这类骨架上试出来的，直接用。
 */
[[nodiscard]] PoseTunings RigidTunings();

/**
 * 权重较粗糙的骨架（hunyuan）：把摆幅整体收小，避免露出刚体三角与自我穿插。
 *
 * 收的是**幅度**不是形状：步态周期、支撑相比例、相位关系全部保持，所以两者
 * 的行走"看起来是同一个动作"，只是小的那个更保守。这也是为什么这些参数在
 * demo 而不是引擎——它们是"这具骨架能承受多少"的经验值。
 */
[[nodiscard]] PoseTunings CoarseSkinnedTunings();

/**
 * Pick the tuple for a naming channel.
 *
 * The mapping is "which asset family" -> "which numbers", and it lives here rather
 * than being inferred from bone count or any other proxy: a proxy would silently pick
 * the wrong tuple for the next asset.
 */
[[nodiscard]] PoseTunings TuningsFor(NamingChannelForTunings channel);

}  // namespace animrigid

#endif  // ANIMRIGID_TUNINGS_H
