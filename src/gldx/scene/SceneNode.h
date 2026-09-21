#ifndef GLDX_SCENE_SCENENODE_H
#define GLDX_SCENE_SCENENODE_H

/**
 * @file SceneNode.h
 * @brief A node in the scene hierarchy (plan "SceneNode"): a Transform plus an
 *        optional renderable and child nodes.
 *
 * No GL here. Mesh / Material are referenced by non-owning pointer - their GPU
 * resources are owned (and destroyed) elsewhere on the render thread, while the
 * node only caches CPU-side transforms and world-space bounds. Children are held
 * by unique_ptr, so a SceneNode is move-only and its address is stable once
 * stored, which keeps pointers into a flattened render list valid across frames.
 *
 * UpdateWorld(parent) recomputes this node's world matrix and world bounding
 * sphere, then recurses - a child transform is expressed in its parent's space,
 * which is what makes hierarchies (an orbiting pivot carrying meshes, etc.)
 * work without any per-object bookkeeping in the application.
 */

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gldx/geometry/Mesh.h"     // MeshData for the local-bound helper
#include "gldx/material/Material.h" // PbrMaterial (non-owning)
#include "gldx/scene/Transform.h"

namespace gldx {

class SceneNode {
public:
    SceneNode() = default;
    explicit SceneNode(Transform local) noexcept : local_(std::move(local)) {}

    SceneNode(const SceneNode&)            = delete;
    SceneNode& operator=(const SceneNode&) = delete;
    SceneNode(SceneNode&&)                 = default;
    SceneNode& operator=(SceneNode&&)      = default;

    // Optional renderable. Pointers must outlive the node (owned by the app's
    // resource scope); either may be null for a pure grouping/pivot node.
    void SetRenderable(const Mesh* mesh, const PbrMaterial* material) noexcept {
        mesh_     = mesh;
        material_ = material;
    }
    // Local-space bounding sphere, set once from the source geometry.
    void SetLocalBounds(const glm::vec3& center, float radius) noexcept {
        localCenter_ = center;
        localRadius_ = radius;
    }

    // Derive a tight-ish local bounding sphere from CPU geometry (Stage A data).
    static void BoundsFromMeshData(const MeshData& data, glm::vec3& outCenter,
                                   float& outRadius) noexcept;

    // Add a child; returns the child reference (stable - stored by unique_ptr).
    SceneNode& AddChild(const Transform& local = {}) {
        children_.push_back(std::make_unique<SceneNode>(local));
        return *children_.back();
    }

    // Refresh this node's world matrix from `parentWorld` and recurse into children.
    void UpdateWorld(const glm::mat4& parentWorld) noexcept;

    // ---- accessors ----
    [[nodiscard]] Transform&       local() noexcept { return local_; }
    [[nodiscard]] const Transform& local() const noexcept { return local_; }
    [[nodiscard]] const glm::mat4& world() const noexcept { return world_; }
    [[nodiscard]] const Mesh*        mesh()     const noexcept { return mesh_; }
    [[nodiscard]] const PbrMaterial* material() const noexcept { return material_; }
    [[nodiscard]] const std::vector<std::unique_ptr<SceneNode>>& children() const noexcept {
        return children_;
    }
    [[nodiscard]] glm::vec3 worldCenter() const noexcept { return worldCenter_; }
    [[nodiscard]] float     worldRadius() const noexcept { return worldRadius_; }
    [[nodiscard]] bool      hasRenderable() const noexcept { return mesh_ && mesh_->valid(); }

    bool visible     = true;
    bool castsShadow = true;
    int  id          = -1;   // free slot the app can use for HUD / picking labels

private:
    Transform local_{};
    const Mesh*        mesh_     = nullptr;
    const PbrMaterial* material_ = nullptr;

    glm::vec3 localCenter_{0.0f};
    float     localRadius_ = 0.5f;

    glm::mat4 world_{1.0f};
    glm::vec3 worldCenter_{0.0f};
    float     worldRadius_ = 0.5f;

    std::vector<std::unique_ptr<SceneNode>> children_;
};

} // namespace gldx

#endif // GLDX_SCENE_SCENENODE_H
