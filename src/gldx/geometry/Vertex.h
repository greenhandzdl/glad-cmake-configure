#ifndef GLDX_GEOMETRY_VERTEX_H
#define GLDX_GEOMETRY_VERTEX_H

/**
 * @file Vertex.h
 * @brief CPU-side vertex layout shared by Mesh / GeometryFactory / ModelLoader.
 *
 * A pure value type: no GL, therefore freely constructible on worker threads
 * (plan section 1.2, Stage A). Mesh::Upload is what eventually ships the bytes
 * to the GPU on the render thread.
 */

#include <array>
#include <cstddef>

#include <glm/glm.hpp>

namespace gldx {

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec3 tangent{1.0f, 0.0f, 0.0f};
    glm::vec4 color{1.0f};

    static constexpr std::size_t kAttributeCount = 5;
};

// Position / normal / tangent / color use 3 or 4 floats; uv uses 2.
// Attribute layout (used by Mesh::Upload):
//   0: position (3f)  -> binding 0
//   1: normal   (3f)  -> binding 1
//   2: uv       (2f)  -> binding 2
//   3: tangent  (3f)  -> binding 3
//   4: color    (4f)  -> binding 4
inline constexpr std::size_t kVertexStrideBytes = sizeof(Vertex);

// Offsets for each attribute inside the vertex.
struct VertexOffsets {
    static constexpr std::ptrdiff_t position = offsetof(Vertex, position);
    static constexpr std::ptrdiff_t normal   = offsetof(Vertex, normal);
    static constexpr std::ptrdiff_t uv       = offsetof(Vertex, uv);
    static constexpr std::ptrdiff_t tangent  = offsetof(Vertex, tangent);
    static constexpr std::ptrdiff_t color    = offsetof(Vertex, color);
};

} // namespace gldx

#endif // GLDX_GEOMETRY_VERTEX_H
