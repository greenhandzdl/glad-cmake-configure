#ifndef GLDX_ANIM_SKINNEDVERTEX_H
#define GLDX_ANIM_SKINNEDVERTEX_H

/**
 * @file SkinnedVertex.h
 * @brief CPU-side vertex layout for a skinned mesh + the four-influence limit.
 *
 * A separate layout instead of an extra field on gldx::Vertex: the engine's
 * static mesh path (Mesh / GeometryFactory / ModelLoader::Load) is used by every
 * other demo and its vertex layout is part of that contract, while skinning is a
 * rig-only concern. Keeping the two apart means a skinned model pays for its
 * weights and a static one does not, and adding bones never shifts the attribute
 * offsets the static path already compiled against.
 *
 * Attributes are handed to GL from SkinnedMesh with these locations:
 *   0: position (3f)  1: normal (3f)  2: uv (2f)
 *   5: boneIndex (4 x GL_UNSIGNED_INT, integer attribute, no normalisation)
 *   6: boneWeight (4f)
 * 3/4 stay reserved for the static path's tangent/color, so a demo can draw both
 * kinds of mesh with the same pipeline object numbering.
 */

#include <cstdint>
#include <vector>

#include <string>

#include <glm/glm.hpp>

namespace gldx {

// GLSL has to hard-code the same number in the blend, so it is a constant here
// rather than a per-vertex dynamic count.
inline constexpr std::size_t kMaxBoneInfluences = 4;

/**
 * 一根骨头是否**只起辅助作用**、不参与可见蒙皮。
 *
 * `_dummy_` / `_shadow_` 是 MMD/PMX 导出的惯用前缀：`_shadow_` 骨骼只给阴影用的简化
 * 网格蒙皮，`_dummy_` 是转轴/扭转的挂点（`腕捩`=arm twist），它们的顶点权重全部由同位的
 * 真实骨骼承担。这类骨头**不影响画面**，却占调色板名额——aigei 的 Lamy 模型 294 根里
 * 有 54 根是这两个前缀，剔掉后正好落进 254 的 uniform 预算。
 *
 * 判据只认前缀，不认具体资产：这是导出器惯例，不是某个模型的骨名。
 * 注意**不能**按"权重很小"来剔：手指末端的权重也很小，但它们是可见的。
 */
[[nodiscard]] inline bool IsHelperBoneName(const std::string& name) {
    return name.rfind("_dummy_", 0) == 0 || name.rfind("_shadow_", 0) == 0 ||
           name.rfind("_ik_", 0) == 0;
}

struct SkinnedVertex {
    glm::vec3  position{0.0f};
    glm::vec3  normal{0.0f};
    glm::vec2  uv{0.0f};
    glm::uvec4 boneIndex{0u, 0u, 0u, 0u};
    glm::vec4  boneWeight{0.0f, 0.0f, 0.0f, 0.0f};
};

struct SkinnedMeshData {
    std::vector<SkinnedVertex> vertices;
    std::vector<std::uint32_t> indices;
};

} // namespace gldx

#endif // GLDX_ANIM_SKINNEDVERTEX_H
