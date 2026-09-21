#ifndef GLDX_VOXEL_MESHGPU_H
#define GLDX_VOXEL_MESHGPU_H

/**
 * @file VoxelMeshGpu.h
 * @brief GPU-side voxel geometry (VAO over VoxelVertex) + per-chunk record.
 *
 * VoxelMesh mirrors Mesh's two-phase contract but for the 4-attribute voxel
 * layout: Upload creates buffers + VAO, Update re-substitutes the storage
 * (chunk remesh after an edit) without touching the VAO. Both render-thread.
 *
 * VoxelChunkGpu is what the voxel render passes iterate: the two meshes
 * (opaque + transparent water faces), the chunk translation (mesher emits
 * chunk-local coords), and a bounding sphere for frustum culling / sorting.
 * The CPU Chunk, the meshing job and this GPU record are linked by index in
 * the demo's chunk grid; gldx stays agnostic of the world storage around it.
 */

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "gldx/core/GLBuffer.h"
#include "gldx/core/VertexArray.h"
#include "gldx/voxel/Chunk.h"
#include "gldx/voxel/ChunkMesher.h"

namespace gldx {

class VoxelMesh {
public:
    VoxelMesh() = default;
    ~VoxelMesh() = default;

    VoxelMesh(const VoxelMesh&)            = delete;
    VoxelMesh& operator=(const VoxelMesh&) = delete;
    VoxelMesh(VoxelMesh&&) noexcept            = default;
    VoxelMesh& operator=(VoxelMesh&&) noexcept = default;

    // Stage B: build VAO/VBO/EBO from CPU mesh data. Render-thread only.
    void Upload(VoxelMeshData&& data);

    // Replace buffer contents after a remesh (render-thread only). Falls back
    // to Upload() the first time; keeps the VAO/attribute wiring intact.
    void Update(VoxelMeshData&& data);

    void Draw() const;

    [[nodiscard]] bool valid() const noexcept { return vao_.valid(); }
    [[nodiscard]] std::size_t indexCount() const noexcept { return indexCount_; }

private:
    VertexArray vao_;
    GLBuffer    vbo_;
    GLBuffer    ebo_;
    std::size_t vertexCount_ = 0;
    std::size_t indexCount_  = 0;
};

// One chunk's renderable state, iterated + sorted by the voxel passes.
struct VoxelChunkGpu {
    VoxelMesh opaque;
    VoxelMesh transparent;
    glm::mat4 model{1.0f};            // translation = chunk origin
    glm::vec3 center{0.0f};           // world-space bounding sphere
    float     radius = Chunk::circumRadius();
    glm::ivec3 origin{0, 0, 0};
    bool hasTransparent = false;
};

} // namespace gldx

#endif // GLDX_VOXEL_MESHGPU_H
