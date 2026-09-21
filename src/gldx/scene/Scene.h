#ifndef GLDX_SCENE_SCENE_H
#define GLDX_SCENE_SCENE_H

/**
 * @file Scene.h
 * @brief Owner of a forest of SceneNode roots + per-frame flattening (plan
 *        "SceneNode"/"Scene" hierarchy).
 *
 * Update() refreshes every root's world transform (parent = identity) and
 * rebuilds two cached lists: the visible renderables the geometry/shadow passes
 * iterate, and the world-space picking spheres. Both hold raw SceneNode* that
 * stay valid because nodes live in unique_ptrs. The Scene itself is pure CPU and
 * is only ever touched from the render thread by the frame orchestration, but it
 * carries no GL state so it could be advanced elsewhere if desired.
 */

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "gldx/scene/SceneNode.h"
#include "gldx/scene/Transform.h"

namespace gldx {

class Scene {
public:
    struct PickTarget {
        glm::vec3 center;
        float     radius = 0.0f;
        SceneNode* node  = nullptr;
    };

    Scene() = default;

    // Add a top-level node; returns a stable reference.
    SceneNode& CreateRoot(const Transform& local = {}) {
        roots_.push_back(std::make_unique<SceneNode>(local));
        return *roots_.back();
    }

    // Recompute world transforms for the whole forest and rebuild the cached
    // renderable / pick lists. Call once per frame before rendering.
    void Update();

    // Visible nodes carrying a renderable, in tree order. Valid until next Update().
    [[nodiscard]] const std::vector<SceneNode*>& Renderables() const noexcept {
        return renderables_;
    }
    // Visible renderables that also cast shadows (for the depth pass).
    [[nodiscard]] const std::vector<SceneNode*>& ShadowCasters() const noexcept {
        return casters_;
    }
    // World bounding spheres for mouse picking, parallel to an index space.
    [[nodiscard]] const std::vector<PickTarget>& PickTargets() const noexcept {
        return pickTargets_;
    }

    [[nodiscard]] std::size_t NodeCount() const noexcept { return renderables_.size(); }

private:
    void Visit(SceneNode& node);

    std::vector<std::unique_ptr<SceneNode>> roots_;
    std::vector<SceneNode*>       renderables_;
    std::vector<SceneNode*>       casters_;
    std::vector<PickTarget>       pickTargets_;
};

} // namespace gldx

#endif // GLDX_SCENE_SCENE_H
