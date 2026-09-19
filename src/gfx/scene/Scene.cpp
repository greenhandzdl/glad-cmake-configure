#include "gfx/scene/Scene.h"

namespace gfx {

void Scene::Update() {
    renderables_.clear();
    casters_.clear();
    pickTargets_.clear();

    for (auto& root : roots_)
        root->UpdateWorld(glm::mat4(1.0f));
    for (auto& root : roots_)
        Visit(*root);
}

void Scene::Visit(SceneNode& node) {
    if (node.visible && node.hasRenderable()) {
        renderables_.push_back(&node);
        if (node.castsShadow)
            casters_.push_back(&node);
        pickTargets_.push_back({node.worldCenter(), node.worldRadius(), &node});
    }
    for (const auto& child : node.children())
        Visit(*child);
}

} // namespace gfx
