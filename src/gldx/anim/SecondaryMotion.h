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
    /**
     * 静止时的常数偏置（pitch 轴）。
     *
     * **注意它的方向**：`target.x` 是绕世界 X 的 pitch，正值 = 布面**向后**仰。
     * 所以这个量不是"往下垂"，而是"固定向后倒一截"。裙摆设 0.30 时，整圈下摆
     * 永久停在身后 17°——无论走多久都那样，读起来就是"裙子一直往后、而且不动"。
     * 想让它垂，就该给它 0（rest 姿态本来就是垂的）。
     */
    float gravity   = 0.0f;
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
    /**
     * 一条链里有几"束"，每束几节 —— 或者更简单：每束的节数。
     *
     * 裙摆是 8 组 × 3 节摊平成一条链，而响应强度应该只取决于**离根多远**，不该
     * 取决于"这是第几组"。所以 gain 用 `index % strandLength` 算深度。
     * 尾巴这种单根链填 1（或 count 本身都行，结果一样）。
     */
    int   strandLength = 1;

    /**
     * 呼吸微动的幅度（弧度）与频率（Hz）。
     *
     * **为什么需要它**：这一整套解算的目标只来自根的加速度（前倾、转向、竖直起伏）
     * 和腿的推挤。角色**站着不动**时这些量全是 0，目标就是常数，弹簧收敛到常数后
     * 链彻底冻结——画面读作"头发是塑料做的"，一动只有整体跟着头走。走路时幅度再
     * 大也救不了静止帧。
     *
     * 所以加一个极慢的周期扰动：幅度按深度递增（末端晃得更多），相位按"束"错开，
     * 这样 12 片裙摆不会像一块板子一起摆。数值要小到"说不出在动，但停下来就发现
     * 它在动"——0.02 rad 在末端约 1 cm。
     */
    float breathe    = 0.0f;
    float breatheHz  = 0.45f;

    /**
     * 惯性驱动：布面**滞后于根的运动**，也就是"风阻"那一项。
     *
     * 没有它，链只是一个把目标角度跟死的弹簧：目标由当前位置算出，位置一变目标就
     * 跟着变，弹簧又快，于是布永远贴在那个静态造型上。原地走的 demo 里根的位移
     * 几乎为零（地面在滚、人没动），驱动项直接是 0 —— 裙摆就只能跟着骨盆刚性平移，
     * 这正是"裙子从来没动过"的观感来源。
     *
     * 现在读的是根的**加速度**（位置二阶差分）：每迈一步身体在上下起伏、左右摆，
     * 加速度非零，布面按 -acc 滞后，于是有了随步频的摆动。
     */
    float inertia    = 0.0f;   // 前向：m/s² -> rad
    float inertiaZ   = 0.0f;   // 竖直：腾空/落地时下摆张开或收拢

    /**
     * 探针**拖动**布面的强度（m/s -> rad）。
     *
     * `inertia` 读的是根的加速度，可惜原地走的 demo 里身体几乎是匀速的：加速度
     * 实测接近 0，增益从 0.06 拉到 0.20，摆幅只从 2.70 cm 变到 2.84 cm。真正在
     * 动的是**腿**，而腿的探针在动、布却只被推不被动，于是裙摆只在腿贴近时才动。
     *
     * 这一项把探针的**速度**也接进来：腿扫过时布被它拖着走，走过去以后靠弹簧回摆。
     * 于是每一步都有一次可见的摆动，而且方向始终跟着腿，不会固定朝一个方向。
     */
    float probeDrag  = 0.0f;

    /**
     * 步态直接驱动：两侧大腿各自的前后摆幅（米，膝盖相对髋的前向位移）与增益。
     *
     * 为什么不能用前面的物理项：这具资产是**短裙**，裙摆贴着骨盆，离它最近的探针
     * 就是髋关节——而原地走时髋根本不平移，膝/踝在下摆下方、权重 w≈0。实测探针
     * 速度加权后恒为 0.000，怎么加增益都不动。
     *
     * 所以按真实做法：把腿部摆动当成**信号**直接下发给裙片，并按裙片绕身体的
     * 角度分配方向（前片跟着前摆的腿往前，后片跟着往后）。代价是它不是能量守恒
     * 的物理，收益是"走路时裙子确实在摆"。
     */
    float gaitSwingL = 0.0f;
    float gaitSwingR = 0.0f;
    float gaitDrive  = 0.0f;
};

/**
 * 一条链的运行时状态。每个资产每个 `RigChainKind` 一份。
 *
 * 保存每节的角速度，所以这个对象**跨帧存活**、不可拷贝给别的资产用。
 */
class ChainSolver {
public:
    /** 按链长度重置状态。链变了（换资产）必须再调一次。 */
    /**
     * 呼吸微动读的时钟，由调用方（demo 的动画时钟）提供。
     *
     * 内部原本是 `clock_ += dt` 自己累加的。那让"呼吸相位"取决于**跑了多少帧**：
     * 同一个 `--freeze-at` 快照，两次运行累加的总时长差一两个 dt，相位就差一点，
     * 头发/尾巴整体偏一点点——2.2 m 视距下 1 mm 正好一像素，于是上万条边缘像素
     * 不一致（实测 1697~14803）。呼吸是装饰性的，让它跟着动画时间走，快照才可复现。
     */
    void SetClock(float seconds) noexcept { clock_ = seconds; }

    /** 诊断用：角度与角速度的和。两次运行之间它不同，就说明解算状态不可复现。 */
    [[nodiscard]] double StateFingerprint() const noexcept {
        double acc = 0.0;
        for (std::size_t i = 0; i < angle_.size(); ++i) {
            acc += static_cast<double>(angle_[i].x) + static_cast<double>(angle_[i].y) +
                   static_cast<double>(angle_[i].z);
            acc += static_cast<double>(omega_[i].x) + static_cast<double>(omega_[i].y) +
                   static_cast<double>(omega_[i].z);
        }
        return acc;
    }

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
        legRadii_.assign(points.size(), radius);
        legRadius_ = radius;
    }

    /**
     * Extra probes with their OWN radius (hands, props, whatever else pokes the cloth).
     *
     * A single radius for every probe is wrong by a factor of two: a thigh is ~11 cm
     * across, a wrist is ~5. Feeding the hands in at thigh radius made the hem balloon
     * out wherever an arm swung past it during a run - measured as the whole skirt
     * flaring into a ball in the front view. The radius belongs to the probe, not to
     * the mesh that owns the chain.
     */
    void AddProbes(const std::vector<glm::vec3>& points, float radius) {
        for (const glm::vec3& p : points) {
            legPoints_.push_back(p);
            legRadii_.push_back(radius);
        }
    }

    /** Drop every probe; call before rebuilding the set each frame. */
    void ClearProbes() {
        legPoints_.clear();
        legRadii_.clear();
    }

    [[nodiscard]] bool active() const noexcept { return !angle_.empty(); }

    // 微动用的内部时钟（秒）。跨帧累加，静止时也走，所以"不动"的姿势里链仍在呼吸。
    float clock_ = 0.0f;

private:
    // Per-joint angular state, in the joint's own rest frame. Kept as a small vec3
    // (pitch, yaw, roll) rather than a quaternion: these are small angles that come
    // from a spring, and decomposing them is how the "which axis is lagging" question
    // stays answerable from a debug dump.
    std::vector<glm::vec3> angle_;
    std::vector<glm::vec3> omega_;
    std::vector<glm::vec3> prevRootPos_;
    glm::vec3              prevRootUp_{0.0f, 1.0f, 0.0f};
    glm::vec3              prevRootVel_{0.0f};
    std::vector<glm::vec3> prevProbePos_;
    // Leg sample points for the cloth-vs-leg push, refreshed every frame by the caller.
    std::vector<glm::vec3> legPoints_;
    std::vector<float>     legRadii_;
    float                  legRadius_ = 0.0f;   // max radius, kept for the quick empty test
    bool                   primed_ = false;
};

}  // namespace gldx

#endif  // GLDX_ANIM_SECONDARYMOTION_H
