#ifndef GLDX_GEOMETRY_MESH_H
#define GLDX_GEOMETRY_MESH_H

/**
 * @file Mesh.h
 * @brief GPU geometry resource (VAO + interleaved VBO + optional EBO).
 *
 * Two-phase ownership:
 *   - MeshData is the pure CPU side (Stage A): freely built on worker threads
 *     by GeometryFactory / ModelLoader.
 *   - Upload() creates the GL buffers + VAO. It must run on the render thread
 *     (Stage B) and is normally invoked through AssetManager's upload queue.
 */

#include <cstdint>
#include <vector>

#include <glad/gl.h>

#include "gldx/core/GLBuffer.h"
#include "gldx/core/VertexArray.h"
#include "gldx/geometry/Vertex.h"

namespace gldx {

// One draw range within a mesh's index buffer (per Assimp submesh / material).
struct GeometryRange {
    std::uint32_t indexOffset = 0;  // start index
    std::uint32_t indexCount  = 0;  // number of indices
    int           materialIndex = -1;
};

struct MeshData {
    std::vector<Vertex>       vertices;
    std::vector<std::uint32_t> indices;
    std::vector<GeometryRange> ranges;  // empty => one implicit whole-mesh range
};

class Mesh {
public:
    Mesh() = default;
    ~Mesh() = default;                       // members' own asserts govern thread affinity

    Mesh(const Mesh&)            = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&&) noexcept            = default;
    Mesh& operator=(Mesh&&) noexcept = default;

    // Stage B: create GL objects from CPU data. Render-thread only.
    void Upload(MeshData&& data);

    // Stage B re-run: replace the buffer contents in place (glBufferData full
    // store re-substitution, i.e. orphan + realloc) while keeping the VAO and
    // its attribute setup. Used by callers that remesh over time (voxel chunks
    // after an edit). Falls back to Upload() when nothing was created yet.
    // The vertex layout is fixed by Vertex, so only the bytes change. Render-
    // thread only; the new MeshData may be built on any worker thread first.
    void Update(MeshData&& data);

    void Draw() const;                       // whole mesh (or all implicit range)
    void DrawRange(const GeometryRange& range) const;

    [[nodiscard]] bool valid() const noexcept { return vao_.valid(); }
    [[nodiscard]] std::size_t vertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] std::size_t indexCount()  const noexcept { return indexCount_; }
    [[nodiscard]] const std::vector<GeometryRange>& ranges() const noexcept { return ranges_; }

private:
    VertexArray vao_;
    GLBuffer    vbo_;
    GLBuffer    ebo_;
    std::vector<GeometryRange> ranges_;
    std::size_t vertexCount_ = 0;
    std::size_t indexCount_  = 0;
};

} // namespace gldx

#endif // GLDX_GEOMETRY_MESH_H
