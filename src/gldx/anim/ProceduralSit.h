/**
 * @file ProceduralSit.h
 * @brief 坐下：席地而坐（不是坐椅子），腿交叉侧收，手撑在身后。
 *
 * ---- 为什么是"坐在地上"而不是"坐在椅子上" ----
 *
 * 这具资产的裙子是个锥体，坐下时它会自然铺开——这正是席地而坐好看的地方，也是
 * 椅子在这套骨架里做不到的事（没有椅子模型，也没有可依赖的坐高）。所以本动作
 * 高度由调用方给：坐姿的骨盆比站姿低 `seatDrop` 米，其余交给地面解算。
 *
 * ---- 为什么这里可以直接平移骨盆（别处不行）----
 *
 * `ProceduralCrouch` 里"平移骨盆"被实测证伪过：整具角色挂在 hips 下，地面解算
 * 又把最低鞋底钉回地板，平移被原样抵消。**但那只在脚是承重件时成立**。坐下的
 * 承重件是臀部，而臀部不参与地面解算（解算只看四个鞋底接触点），所以这里给
 * 骨盆一个显式的下沉是唯一能把人"放"到地上的办法——平移项不会被抵消。
 *
 * 效果：地面解算会把整具角色**抬起**（最低鞋底在地板之上），这正是坐姿该有的
 * 样子（脚离地或轻搭在地上）。demo 的 `report` 表里 sit 的 `hip` 明显低于 idle，
 * 而 `sole` 略大于 0。
 *
 * ---- 姿态 ----
 *
 *   - 双腿：髋屈 ~1.4 rad、膝屈 ~1.9 rad，左右略微不对称并外展，形成"盘腿侧坐"
 *     的轮廓（参考资产没有脚趾骨以外的细节，腿交叉得太真反而会露出穿模）。
 *   - 躯干：略前倾、轻微侧倾，手撑在身后。
 *   - 呼吸：与蹲姿同一个套路——**只动上身**，腿不动，所以重心不飘。
 */

#ifndef GLDX_ANIM_PROCEDURALSIT_H
#define GLDX_ANIM_PROCEDURALSIT_H

#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"

namespace gldx {

struct SitTuning {
    // ---- 坐姿是**跪坐**（seiza），不是盘腿 ----
    //
    // 参考图里：小腿平折在地面上、大腿竖直略前倾、臀部坐在脚跟上、双手撑在身前。
    // 关键一条是 `hipBend` 很小——大腿几乎竖直；`kneeBend` 很大——小腿完全折回。
    // 这两条合起来才有"跪坐"的轮廓。
    //
    // 之前做的是**盘腿**（髋前屈 1.75 + 膝屈 1.90），那是另一种坐姿，而且配这条
    // 及膝圆裙时腿被完全盖住，画面读不出"坐"。跪坐的膝与小腿都在裙摆下方，能看见。
    // 左右几乎对称：跪坐是双脚并拢的姿势。
    float legAsymmetry = 0.03f;  // rad
    // ---- 鸭子坐（W-sit），按参考图 ----
    //
    // 参考图的读法：**膝盖在前方、两膝略分开**；**小腿从膝向后外侧折回**，贴在
    // 地面上，两脚落在臀部的左右两侧；脚背朝上。正面看两腿围成 W，侧面看膝盖在
    // 身前、脚在身后。
    //
    // ---- 四个数就是全部：FK（正运动学），每根骨头只转一次 ----
    //
    // 之前用"目标世界方向 + 对齐"做过十几轮，每次都换一种错法（盘腿、劈叉、悬空），
    // 因为小腿是子骨、继承父级旋转，而目标方向是在世界系里给的——两者不一致时
    // 补偿矩阵的方向难推，改一个数就整体变形。
    //
    // 现在退回最原始也最可预测的做法：每根骨头在**局部系**里绕固定轴转一个角度，
    // 子级自动继承父级的旋转。符号沿用全局约定（绕 side 的正角让朝下的骨头往后摆）：
    //
    //   hip   髋角（负 = 前屈，大腿往前倒；正 = 后伸）
    //   knee  膝角（正 = 小腿相对大腿往后折）
    //   out   大腿外展（绕 forward，把膝盖向侧面打开）
    //   ankle 踝角（正 = 跖屈，脚背朝下）
    //
    // 鸭子坐 = 髋略前屈（大腿近乎水平朝前）+ 膝折回约 90°（小腿向后落在臀侧）
    //          + 大腿小幅外展（两膝并拢）。从正面看两腿围成 W。
    // -1.34 rad 是**77°**，不是 0.20。这一行曾经写成 -0.20（11.5°），与它自己的
    // 注释差了 6.7 倍：大腿基本还是竖直的，于是"坐下"只是把膝盖弯了一点，角色整个
    // 站在原高度（实测蒙皮后顶点 y 跨度 [-0.02, 2.07]，和站姿一样高），裙摆悬在
    // 半空。注释描述的动作从来没有被做出来过。
    float hip    = 0.45f;    // rad，正值 = 前屈（大腿向前倒到水平）
    float knee   = 2.50f;   // rad，小腿折回，落在臀部两侧
    float out    = 0.0f;    // 遗留项（绕 up = 扭转），W 坐不再用它做外展
    float ankle  = -0.55f;  // rad，踝角
    /**
     * 大腿外展（绕 `forward`，rad）。
     *
     * 必须绕 `forward`，不能绕 `up`：对一根**朝下**的腿骨，`up` 与骨头长轴平行，
     * 绕它转只是**扭转**，四元数不改变骨头朝向。实测 `out` 从 0.30 扫到 0.55，
     * 大腿的 side 分量恒为 -0.02（一点没动），就是这条的证据。
     *
     * `out` 保留为同名旧参数（它在代码里确实是绕 up 转的，即扭转），新增本项才是
     * 真正的"把膝盖向侧面打开"。
     */
    float splay  = 0.95f;
    /**
     * 大腿扭转（绕 `up`，rad，符号随左右）。
     *
     * 决定小腿往**哪个方向**折：膝轴跟着大腿扭转，同样一个膝角可以把脚折到臀侧，
     * 也可以把脚折到身体正后方。W 坐要求脚落在臀部两侧，所以这一项是必需的。
     */
    float twist  = -0.50f;
    // ---- 坐高：由**调用方的接触点**决定，本类不写 ----
    //
    // 坐姿的骨盆高度不在这里平移。`GroundContact` 只认它被给的接触点，而四个鞋底
    // 接触点回答不了"坐下了没有"——实测：cross-leg 姿态下解算报告 sole = 0.000，
    // 而最低的骨头在地板下 0.93 m。
    //
    // 所以调用方要用 `GroundContact::AddBodyContact(hips, hipBoxCorners, 8)` 给坐姿
    // 一个**以臀为承重件**的解算器（demo 里是 `Character::seatedGround`）。那时解算
    // 自动把骨盆放到地板上，本类只管关节角。
    //
    // 曾经这里有一个 `seatDrop`（米）做平移：单位错了（outPose 是骨骼单位，x0.01），
    // 而且与解算重复。删掉。

    // 躯干：坐地时上身直立、略前倾。盘腿时手撑在身侧，所以重心是稳的。
    float spineLean = 0.1f;   // rad
    float spineTilt = 0.05f;   // rad，侧倾
    float headLift  = 0.08f;   // rad

    // 手臂：肘微屈、手撑在身侧偏后。这是坐地时重心的第二种支撑，也解释了为什么
    // 上身可以只前倾 0.14 rad。
    float armDown   = 1.00f;   // rad，A-pose 下的基准
    float armBack   = 0.55f;   // rad，向后（撑在身侧后方）
    float elbowBend = 0.50f;   // rad
    float armOut    = 0.30f;   // rad，前臂外展，手落到身体两侧
    float wristBack = 0.30f;   // rad，手掌向后翻，撑地

    // 呼吸（循环，只动上身）。
    float breathSeconds = 3.6f;
    float breathSpine   = 0.020f;  // rad
    float breathSway    = 0.012f;  // rad
    float breathHead    = 0.026f;  // rad
};

/**
 * 席地而坐。循环（呼吸），`ContactPhase()` 为 -1：坐着没有"落脚瞬间"。
 *
 * 骨盆下沉是本类的姿态的一部分（`seatDrop`），但**不写进关节**——它与地面解算
 * 不冲突：解算只看鞋底接触点，坐姿里最低的鞋底在地板之上，解算于是把整具角色
 * 抬起，而抬起的量正是"脚够不到地"的那一段。
 */
class ProceduralSit final : public AnimPoseSource {
public:
    ProceduralSit(const Skeleton& skeleton, const RigProfile& rig,
                  const std::vector<glm::mat4>& restWorld, const SitTuning& tuning);

    [[nodiscard]] const char* name() const noexcept override { return "sit"; }
    [[nodiscard]] float Duration() const noexcept override { return tuning_.breathSeconds; }
    [[nodiscard]] bool Looping() const noexcept override { return true; }
    [[nodiscard]] float ContactPhase() const noexcept override { return -1.0f; }

    void Sample(float t, std::vector<glm::mat4>& outPose) const override;

    [[nodiscard]] const SitTuning& tuning() const noexcept { return tuning_; }

private:
    const Skeleton&               skeleton_;
    const RigProfile&             rig_;
    const std::vector<glm::mat4>& restWorld_;
    SitTuning                     tuning_;
};

} // namespace gldx

#endif // GLDX_ANIM_PROCEDURALSIT_H
