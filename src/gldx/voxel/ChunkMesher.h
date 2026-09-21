#ifndef GLDX_VOXEL_CHUNKMESHER_H
#define GLDX_VOXEL_CHUNKMESHER_H

/**
 * @file ChunkMesher.h
 * @brief CPU voxel -> triangle conversion for one chunk (no GL, worker-safe).
 *
 * The mesher walks the chunk's cells, emits a quad for every face that borders
 * a non-opaque neighbour (interior faces vanish), and packs per-vertex data
 * for the block texture array + a baked lighting channel:
 *   - per-face directional shading (top brightest, bottom darkest), like the
 *     classic voxel games do instead of a realtime light loop;
 *   - per-vertex ambient occlusion from the 3 cells around each corner
 *     (side1/side2/corner rule, 4 levels), with the quad diagonal flipped so
 *     the AO gradient never tears across the wrong pair of triangles.
 *
 * Vertices are chunk-LOCAL (0..kChunkSize); the GPU record carries the chunk
 * translation as uModel, so editing a block remeshes only that chunk.
 * Opaque and transparent (water) faces are emitted into separate buffers so
 * the two render passes can consume them without re-splitting.
 *
 * Known simplifications (deliberate, documented): no greedy meshing yet —
 * face-count reduction is the natural next optimisation once the pipeline is
 * proven; cross-chunk AO reads neighbours through the IVoxelSource, so two
 * neighbouring chunks can disagree on border AO only if they are meshed from
 * different source snapshots (the demo remeshes neighbours on border edits).
 */

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "gldx/voxel/BlockRegistry.h"
#include "gldx/voxel/Chunk.h"

namespace gldx {

// Compact voxel vertex, 48 B. Layout (offsets) is mirrored by VoxelMesh's VAO
// and the kVoxelVertex attribute declarations:
//   0: position (3f)   1: uv (2f)   2: layer (1f)   3: light (1f)
struct VoxelVertex {
    glm::vec3 position{0.0f};
    glm::vec2 uv{0.0f};
    float     layer = 0.0f;   // texture-array slice
    float     light = 1.0f;   // face shade * vertex AO, [0..1]

    static constexpr std::size_t kAttributeCount = 4;
};

struct VoxelMeshData {
    std::vector<VoxelVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct VoxelChunkMesh {
    VoxelMeshData opaque;
    VoxelMeshData transparent;
};

class ChunkMesher {
public:
    explicit ChunkMesher(const BlockRegistry& registry) : registry_(registry) {}

    // Mesh chunkCells (a kChunkSize^3 id array, index = VoxelIndex) anchored at
    // chunkOrigin, sampling world neighbours (other chunks / void) via source.
    [[nodiscard]] VoxelChunkMesh Build(const std::uint16_t* chunkCells,
                                       glm::ivec3 chunkOrigin,
                                       const IVoxelSource& source) const;

private:
    const BlockRegistry& registry_;
};

} // namespace gldx

#endif // GLDX_VOXEL_CHUNKMESHER_H
