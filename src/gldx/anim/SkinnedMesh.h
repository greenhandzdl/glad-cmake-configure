#ifndef GLDX_ANIM_SKINNEDMESH_H
#define GLDX_ANIM_SKINNEDMESH_H

/**
 * @file SkinnedMesh.h
 * @brief GPU owner of one skinned draw (VAO + interleaved VBO + EBO).
 *
 * The static gldx::Mesh cannot carry this geometry: its attribute layout is
 * fixed to position/normal/uv/tangent/color and it has no notion of bone
 * indices. Rather than widening that layout for everyone, a skinned mesh gets
 * its own small owner with the same two-phase shape (SkinnedMeshData on the
 * worker thread, Upload on the render thread) and the same move-only RAII rule.
 *
 * The bone palette is deliberately NOT owned here: A palette belongs to the
 * skeleton and is shared by every mesh of one character, while the demo may draw
 * the same palette through more than one program. So the caller uploads
 * `uniform mat4 uBones[kMaxBonePalette]` itself (Skeleton::ComputeSkinning hands
 * it the matrices) and this class stays a geometry owner.
 */

#include <cstddef>

#include <glad/gl.h>

#include "gldx/anim/SkinnedVertex.h"
#include "gldx/core/GLBuffer.h"
#include "gldx/core/VertexArray.h"

namespace gldx {

class SkinnedMesh {
public:
    SkinnedMesh() = default;
    ~SkinnedMesh() = default;                 // members' destructors assert thread affinity

    SkinnedMesh(const SkinnedMesh&)            = delete;
    SkinnedMesh& operator=(const SkinnedMesh&) = delete;
    SkinnedMesh(SkinnedMesh&&) noexcept            = default;
    SkinnedMesh& operator=(SkinnedMesh&&) noexcept = default;

    // Stage B: create the GL objects from CPU data. Render-thread only.
    void Upload(SkinnedMeshData&& data);

    void Draw() const;                        // whole mesh

    [[nodiscard]] bool        valid() const noexcept { return vao_.valid(); }
    [[nodiscard]] std::size_t vertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] std::size_t indexCount() const noexcept { return indexCount_; }

private:
    VertexArray vao_;
    GLBuffer    vbo_;
    GLBuffer    ebo_;
    std::size_t vertexCount_ = 0;
    std::size_t indexCount_ = 0;
};

} // namespace gldx

#endif // GLDX_ANIM_SKINNEDMESH_H
