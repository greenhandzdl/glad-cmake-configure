module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

void SceneNode::BoundsFromMeshData(const MeshData& data, glm::vec3& outCenter,
                                   float& outRadius) noexcept {
    if (data.vertices.empty()) {
        outCenter = glm::vec3(0.0f);
        outRadius = 0.5f;
        return;
    }
    glm::vec3 mn(data.vertices[0].position), mx = mn;
    for (const auto& v : data.vertices) {
        mn = glm::min(mn, v.position);
        mx = glm::max(mx, v.position);
    }
    outCenter = (mn + mx) * 0.5f;
    outRadius = glm::length(mx - outCenter);
}

void SceneNode::UpdateWorld(const glm::mat4& parentWorld) noexcept {
    world_ = parentWorld * local_.LocalMatrix();

    const glm::vec4 c = world_ * glm::vec4(localCenter_, 1.0f);
    worldCenter_ = glm::vec3(c);
    worldRadius_ = localRadius_ * MaxAxisScale(world_);

    for (const auto& child : children_)
        child->UpdateWorld(world_);
}

} // namespace gfx
