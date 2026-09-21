#ifndef GLDX_GEOMETRY_MODEL_H
#define GLDX_GEOMETRY_MODEL_H

/**
 * @file Model.h
 * @brief A rendered model: one or more GPU meshes plus texture bindings.
 *
 * Produced by AssetManager after Assimp (Stage A) has been uploaded (Stage B).
 * Only valid on the render thread.
 */

#include <memory>
#include <string>
#include <vector>

#include "gldx/geometry/Mesh.h"
#include "gldx/texture/Texture2D.h"

namespace gldx {

struct Model {
    std::vector<std::shared_ptr<Mesh>> meshes;
    std::vector<std::shared_ptr<Texture2D>> textures;  // referenced color maps
    std::string name;
    float scale = 1.0f;
    glm::vec3 translation{0.0f};
};

} // namespace gldx

#endif // GLDX_GEOMETRY_MODEL_H
