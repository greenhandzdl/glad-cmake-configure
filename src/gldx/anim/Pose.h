#ifndef GLDX_ANIM_POSE_H
#define GLDX_ANIM_POSE_H

/**
 * @file Pose.h
 * @brief 程序化摆姿态的原语：把"绕世界轴转 × 度"折进某根骨头的局部系。
 *
 * 纯 CPU（std + glm + gldx::Skeleton），可在工作线程构建，不含 GL。
 *
 * ---- 这一层全部的秘密就一句话 ----
 *
 * 姿势是一组**局部**矩阵，而 FK 的关系是
 *
 *     world(bone) = world(parent) * local(bone)                // Skeleton::ComputeWorld
 *
 * 我们想说的是"让这根骨绕**世界**轴 w 额外转 θ 度"。把额外旋转记作 Rw，希望
 * world(bone) 变成 Rw * world(bone)。代入上式：
 *
 *     Rw * world(bone)
 *       = Rw * world(parent) * local(bone)
 *       = world(parent) * (restWorld^-1 * Rw * restWorld) * local(bone)
 *
 * 于是补进 local 的增量就是
 *
 *     Δlocal = restWorld^-1 * Rw * restWorld
 *
 * 而交叉相似变换只换轴不换角：`R^-1 * Rw * R` 的转轴正是 `R^-1 * w`。所以
 *
 *     Δlocal = rotate(θ, transpose(mat3(restWorld)) * w)
 *
 * ——**这就是"绕骨骼自己的关节点转"的全部道理**，不需要任何额外特判：Δlocal 是右乘在
 * local 上的，而 local 的旋转中心就是骨骼自己的原点，即关节点。反过来说，如果直接把 Rw
 * 乘到 world(bone) 的左边（`glm::rotate(θ, w) * world`），得到的是"整根骨头绕**世界原点**
 * 划一个 θ 弧"——角色会像被一根绳子从世界原点甩着转，而不是在原地摆关节。这是最常见的
 * 写错法，而且画面像"相机在动"或"模型在漂"，很难第一时间定位到姿态代码。
 *
 * 为什么用 transpose 而不是 inverse：restWorld 的旋转部分是正交阵（三个轴都是单位长且
 * 两两垂直，见 RigProfile 的量法），对正交阵 R^-1 == R^T，转置是免费的。代价是：**如果
 * 骨架里有非均匀缩放**（FBX 里确实会出现），mat3 就不再正交，转置只是"方向对了但长度
 * 错了"的近似——PoseRotation 里会重新 normalize 轴向量，于是旋转角仍然精确、方向仍然正确，
 * 只有"缩放被带进旋转"这种组合误差被丢掉。对步态这种只关心旋转的用法，这比引入一次
 * 矩阵求逆（以及它对奇异矩阵的静默失败）划算得多。
 *
 * ---- 为什么要 reset 而非"增量累加" ----
 *
 * Pose 只是一组矩阵，没有状态机。每次重摆都必须先 ResetToRest() 把整组矩阵填回 rest local，
 * 再往上叠这次要的旋转。若在上一帧的结果上继续乘，误差会累积成"越走越歪"，而且 `--freeze-at`
 * 不再可复现（帧数不同 → 叠的层数不同 → 同一时刻姿态不同）。这一点值得写在显眼处，因为
 * 增量累加在单帧里看起来是对的。
 */

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/Skeleton.h"

namespace gldx {

/**
 * 一根骨骼的"绕世界轴 w 转 θ"折进局部系之后的那**一个**旋转矩阵。
 *
 * @param restWorldBone  该骨骼在 bind 姿态下的世界矩阵（`Skeleton::RestWorld(i)`），
 *                       注意是 **rest** 而不是当前帧的 world。用当前帧的 world 能跑，
 *                       但轴会随父骨一起转，于是子骨的旋转在视觉上被父骨的旋转"二次放大"。
 * @param worldAxis      世界空间的旋转轴，无需归一化（内部会归一化）。
 * @param radians        弧度，符号按右手定则绕 worldAxis。
 * @return 纯旋转矩阵（无平移、无缩放）。退化输入时返回单位阵而不是 NaN：见下。
 *
 * 退化输入（restWorld 的旋转部分是零/近零，通常是骨骼没绑上、或 rest 矩阵还没算出来的
 * 占位单位阵）时返回**单位阵**。这不是"静默失败"而是刻意的取舍：姿态代码里一次 no-op
 * 的代价是"这根骨头没动"，而 NaN 的代价是整具骨架的每一根子骨都变成 NaN、网格整个消失，
 * 且 NaN 不会触发任何断言。要发现"没动"，用 RigProfile::Missing() 在加载时就报出来，
 * 而不是指望这里报警——两个不同的问题交给两个不同的地方守。
 */
[[nodiscard]] glm::mat4 PoseRotation(const glm::mat4& restWorldBone, const glm::vec3& worldAxis,
                                     float radians);

/**
 * 把 @p outPose 填成整具骨架的 rest 姿态（一个 local 矩阵一根骨头，按骨骼顺序）。
 *
 * 调用方每帧重摆之前先调它。等价于 `outPose.assign(... restLocal ...)`，但把"空 pose == bind
 * pose"这条不变式集中到一处：`Skeleton::ComputeWorld` 对短的 pose 向量也是这么补的，两边
 * 必须一致，否则"少摆几根骨头"和"显式摆成 rest"会给出不同结果。
 */
void ResetToRest(const Skeleton& skeleton, std::vector<glm::mat4>& outPose);

/**
 * 在 @p pose 里给第 @p bone 根骨叠加一次世界轴旋转：
 *
 *     pose[bone] = pose[bone] * PoseRotation(restWorld(*), worldAxis, radians)
 *
 * 是**右乘**而不是左乘——右乘才是在骨骼自己的局部系里追加，因而绕它自己的关节点转（见文件头）。
 *
 * 参数：
 *  - pose：已经 ResetToRest 过的姿势（长度按 `Skeleton::size()`）。
 *  - restWorld：每根骨骼的 rest world，`restWorld[i] == skeleton.RestWorld(i)`，由调用方用
 *    ComputeRestWorlds 缓存一次（换资产时重建）。用**当前帧**的 world 而不是 rest world 也能
 *    编译、也能跑，但轴会随父骨转，子骨的旋转在视觉上被父骨"二次放大"。
 *  - bone：骨骼下标；小于 0 时静默 no-op。
 *  - radians：0 时同样 no-op（省一次矩阵乘，也避免给浮点加 0 误差）。
 *
 * 为什么 bone < 0 静默：这个函数是"可选槽位"的唯一消费口，而 RigProfile 的槽位天生可能为 -1
 * （资产没这根骨头）。若在这里 assert，任何不完整的骨架都会崩；若在这里打印，每帧每根骨头
 * 一行会把日志冲掉。**代价是真实的**：槽位漏绑时这里什么都不说，症状是"那部分肢体看着僵"。
 * 所以这条静默必须由加载期自检补偿——`RigProfile::Missing()` / `BindingReport()`。历史上这个
 * demo 正因为这条静默而让 hand/clavicle 的漏绑活过了几轮 review。
 *
 * pose / restWorld 越界（调用方传了半截容器）同样按 no-op 处理：不至于读越界，最多是"这次
 * 旋转没生效"。注意它只挡得住"容器太短"，挡不住"传了一具**别的**骨架的 restWorld"——长度
 * 对上时只会绕错轴转，而绕错轴不会崩、不会 NaN，画面是"这个角色的关节在往奇怪的方向扭"。
 */
void RotateBone(std::vector<glm::mat4>& pose, const std::vector<glm::mat4>& restWorld, int bone,
                const glm::vec3& worldAxis, float radians);

/**
 * @p restWorld 缓存：`out[i] = skeleton.RestWorld(i)`，长度 = 骨骼数。
 *
 * 为什么让调用方自己缓存而不是每次 RotateBone 内部去调 `Skeleton::RestWorld`：后者每次调用
 * 都要沿 parent 链往上累乘（根到底），而一帧里同一具骨架的同一根骨头可能被转好几次，一具
 * 20+ 骨的骨架一帧几十次重算，纯属白算；更重要的是**它是纯函数**，缓存的正确性由这条性质
 * 保证，不随帧变化，缓存一次到析构都不会失效（换资产要重新建 RigProfile 时一起重建）。
 * 放在这里是因为"rest world 是缓存"这件事属于姿态层的知识，不属于 Skeleton —— Skeleton 提供
 * 的是"可以随时问"的接口，不该反过来假设调用方会缓存。
 */
void ComputeRestWorlds(const Skeleton& skeleton, std::vector<glm::mat4>& outRestWorld);
[[nodiscard]] std::vector<glm::mat4> RestWorlds(const Skeleton& skeleton);

} // namespace gldx

#endif // GLDX_ANIM_POSE_H
