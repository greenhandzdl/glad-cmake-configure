module;

#include "gldx/gmf.hpp"

#include <cstddef>

module gldx;

namespace gldx {

namespace {

// 轴长闸门。1e-6 只用来挡"零向量/未初始化的 rest 矩阵"，不是几何判据：正常的 rest world
// 旋转部分每列长度都是 1，差几个数量级。
inline constexpr float kAxisEpsilon = 1e-6f;

} // namespace

glm::mat4 PoseRotation(const glm::mat4& restWorldBone, const glm::vec3& worldAxis, float radians) {
    // 交叉相似变换只换轴不换角：Δlocal 的转轴 = restWorld^-1 * w。restWorld 的旋转部分是正交的，
    // 所以逆 = 转置，这里省掉一次求逆（对奇异矩阵求逆会静默产出 inf/NaN，转置不会）。
    const glm::mat3 basis = glm::mat3(restWorldBone);
    const glm::vec3 axis  = glm::transpose(basis) * worldAxis;

    // 归一化不可省：worldAxis 由调用方给，可能没归一（例如两骨位置相减得到的方向）。
    // 不归一化的后果是"转的角度不对"，而且是随资产缩放悄悄不对——没有报错、没有 NaN，
    // 只有"这个角色的步幅怎么这么大"。glm::rotate 内部不会替我们归一化。
    const float len = glm::length(axis);
    if (!(len > kAxisEpsilon)) {
        // 写成 !(len > eps) 而不是 len < eps：这样 len 是 NaN 时也落进这个分支。NaN < eps
        // 为 false，会一路穿过去变成 rotate(NaN) 再污染整棵子树。
        return glm::mat4(1.0f);
    }
    return glm::rotate(glm::mat4(1.0f), radians, axis / len);
}

void ResetToRest(const Skeleton& skeleton, std::vector<glm::mat4>& outPose) {
    const std::vector<Bone>& bones = skeleton.bones();
    outPose.resize(bones.size());
    for (std::size_t i = 0; i < bones.size(); ++i) outPose[i] = bones[i].restLocal;
}

void RotateBone(std::vector<glm::mat4>& pose, const std::vector<glm::mat4>& restWorld, int bone,
                const glm::vec3& worldAxis, float radians) {
    // 两个提前返回都故意静默，理由见 Pose.h：bone < 0 是"这具骨架没有这个槽位"的正常情况，
    // 不是错误；真正的体检由 RigProfile::Missing() 在加载期完成。
    if (bone < 0 || radians == 0.0f) return;
    const std::size_t index = static_cast<std::size_t>(bone);
    if (index >= pose.size() || index >= restWorld.size()) return;

    // 右乘：局部系里追加，因此旋转中心是骨骼自己的原点（关节点）。左乘会绕世界原点转，
    // 见 Pose.h 文件头的推导。
    pose[index] = pose[index] * PoseRotation(restWorld[index], worldAxis, radians);
}

void ComputeRestWorlds(const Skeleton& skeleton, std::vector<glm::mat4>& outRestWorld) {
    outRestWorld.resize(skeleton.size());
    for (std::size_t i = 0; i < skeleton.size(); ++i) {
        outRestWorld[i] = skeleton.RestWorld(static_cast<int>(i));
    }
}

std::vector<glm::mat4> RestWorlds(const Skeleton& skeleton) {
    std::vector<glm::mat4> restWorld;
    ComputeRestWorlds(skeleton, restWorld);
    return restWorld;
}

} // namespace gldx
