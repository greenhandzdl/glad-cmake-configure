#ifndef GFX_GEOMETRY_INSTANCEDMESH_H
#define GFX_GEOMETRY_INSTANCEDMESH_H

/**
 * @file InstancedMesh.h
 * @brief GPU-instanced geometry (plan "InstancedMesh"): one base mesh drawn
 *        many times with a per-instance model matrix + colour.
 *
 * Owns a VAO over a geometry VBO/EBO (positions + normals only are bound) plus
 * an instance buffer whose mat4 occupies vertex attrib locations 4-7 and whose
 * colour sits at location 8, all with divisor 1. Pair with shaders::
 * kInstancedVertex/kInstancedFragment (see ShaderLib.h) and set the block
 * binding LightingBlock->1 like the PBR program.
 *
 * Two-phase: MeshData + the instance vector are CPU-side (Stage A); Create /
 * SetInstances do the gl* uploads (Stage B, render thread).
 */

#include <cstdint>
#include <utility>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "gfx/core/GLBuffer.h"
#include "gfx/core/VertexArray.h"
#include "gfx/geometry/Mesh.h"

namespace gfx {

struct Instance {
    glm::mat4 model{1.0f};
    glm::vec4 color{1.0f};
};

class InstancedMesh {
public:
    InstancedMesh() = default;
    InstancedMesh(const InstancedMesh&)            = delete;
    InstancedMesh& operator=(const InstancedMesh&) = delete;
    InstancedMesh(InstancedMesh&&) noexcept            = default;
    InstancedMesh& operator=(InstancedMesh&&) noexcept = default;

    // Build geometry + instance buffers and the VAO. Render-thread only.
    bool Create(MeshData geometry, std::vector<Instance> instances);

    // Replace the instance array (e.g. per-frame animation). Render-thread only.
    void SetInstances(std::vector<Instance> instances);

    void Draw() const;   // glDrawElementsInstanced (render thread)

    [[nodiscard]] bool          valid() const noexcept { return vao_.valid(); }
    [[nodiscard]] std::uint32_t count() const noexcept { return count_; }

private:
    void UploadInstances();

    VertexArray vao_;
    GLBuffer    geoVbo_, ebo_, instVbo_;
    std::uint32_t indexCount_ = 0;
    std::uint32_t count_ = 0;
    std::vector<Instance> instances_;
};

} // namespace gfx

#endif // GFX_GEOMETRY_INSTANCEDMESH_H
