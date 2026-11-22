#ifndef GLDX_ANIM_SECONDARYMOTION_H
#define GLDX_ANIM_SECONDARYMOTION_H

/**
 * @file SecondaryMotion.h
 * @brief 次级运动解算：尾巴 / 裙摆 / 长发 / 兽耳的**延迟跟随**。
 *
 * ## 这是什么，不是什么
 *
 * 这里解的是"身体动了、这些附件晚一点跟上"的那部分运动。它**不是**布料物理
 * （没有碰撞、没有自碰撞、没有弯曲刚度），也不是完整的 MMD 物理。给定
 *
 *   · 每帧根关节的世界位移与朝向变化（从已解算的 pose 读），
 *   · 每条链每节的长度与静止朝向，
 *
 * 算出每节的**延迟角度**，写回 pose 的局部矩阵。因为它只依赖上一帧状态与这一帧
 * 的根运动，所以是**有状态**的——这是它没有做成 `AnimPoseSource` 的原因
 * （那个接口要求 `Sample(t)` 是纯函数，而次级运动按定义不是 t 的函数）。
 *
 * ## 为什么必须是 optional
 *
 * 不带尾巴的角色不该被要求有尾巴骨。`RigProfile::RigChain::count == 0` 是正常
 * 情况，`Solve()` 对空链直接返回。所有调用点都写成 `if (chain.present())`。
 *
 * ## 参数是资产属性，不是引擎常数
 *
 * `ChainTuning` 的每个值都在某一具骨架上试出来的（多少节、多长、多软）。和
 * `WalkTuning` 一样：形状进引擎，数值由调用方给。
 */

#include <glm/glm.hpp>

#include <vector>

#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

/**
 * 一条链的物理参数。
 *
 * 三个量就够了，因为它们对应三种能被看见的差别：
 *   · `stiffness` 决定"多快跟上"：小 = 裙摆很软、动起来甩；大 = 硬挺
 *   · `damping`   决定"多久停下"：小 = 一直颤；大 = 一下就僵住
 *   · `gravity`   决定"静止时朝哪垂"：裙子要垂下来，兽耳要立起来
 */
struct ChainTuning {
    float stiffness = 0.35f;   // 0..1，每帧向目标角度靠拢的比例
    float damping   = 0.82f;   // 0..1，速度衰减
    float gravity   = 0.25f;   // 静止时朝重力方向偏的量（0 = 完全跟随父级）
    /**
     * 静止时把链**向身体中线收拢**的量（roll 轴），负值 = 向内。
     *
     * 用途是让裙摆离手臂远一点：手臂垂在身侧时，手到中线的距离只有十几厘米，而裙摆
     * 半径更大，于是手插进裙面。与其一直抬手臂（抬多了姿态就不自然），让裙摆自己也
     * 收一点，两边各让一半。
     *
     * 只影响**静止偏置**，不影响动态响应，所以走路时的摆动照旧。
     */
    float tuck      = 0.0f;
};

/**
 * 一条链的运行时状态。每个资产每个 `RigChainKind` 一份。
 *
 * 保存每节的角速度，所以这个对象**跨帧存活**、不可拷贝给别的资产用。
 */
class ChainSolver {
public:
    /** 按链长度重置状态。链变了（换资产）必须再调一次。 */
    void Reset(const RigProfile::RigChain& chain);

    /**
     * 解算一帧。
     *
     * @param chain   骨骼下标链（`RigProfile::chains[kind]`）
     * @param local   （输入输出）这一帧的局部姿势矩阵，解算结果写回去
     * @param world   （输入）这一帧的世界矩阵，**必须已经算好**——链的根运动从
     *                这里读，所以调用顺序是 ComputeWorld -> Solve -> 重算世界
     * @param restWorld 绑定的世界矩阵（静止朝向）
     * @param tuning  本条链的参数
     * @param dt      真实秒数。夹在 [0, 0.05] 由调用方负责（见 demo 的 dt 夹紧）
     */
    void Solve(const RigProfile::RigChain& chain, std::vector<glm::mat4>& local,
               const std::vector<glm::mat4>& world, const std::vector<glm::mat4>& restWorld,
               const ChainTuning& tuning, float dt);

    /**
     * Tell the solver where the legs are, so the hem gets pushed outward by them.
     *
     * Without this the skirt only reacts to the ROOT's acceleration (start, turn,
     * landing) and is blind to the legs swinging inside it - so the leg comes through
     * the surface while the cloth does not move at all, which reads as "the skirt is
     * rigid". Points are world space; `radius` is the effective leg thickness in metres.
     */
    void SetLegProbes(const std::vector<glm::vec3>& points, float radius) {
        legPoints_ = points;
        legRadius_ = radius;
    }

    [[nodiscard]] bool active() const noexcept { return !angle_.empty(); }

private:
    // Per-joint angular state, in the joint's own rest frame. Kept as a small vec3
    // (pitch, yaw, roll) rather than a quaternion: these are small angles that come
    // from a spring, and decomposing them is how the "which axis is lagging" question
    // stays answerable from a debug dump.
    std::vector<glm::vec3> angle_;
    std::vector<glm::vec3> omega_;
    std::vector<glm::vec3> prevRootPos_;
    glm::vec3              prevRootUp_{0.0f, 1.0f, 0.0f};
    // Leg sample points for the cloth-vs-leg push, refreshed every frame by the caller.
    std::vector<glm::vec3> legPoints_;
    float                  legRadius_ = 0.0f;
    bool                   primed_ = false;
};

}  // namespace gldx

#endif  // GLDX_ANIM_SECONDARYMOTION_H
