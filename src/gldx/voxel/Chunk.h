#ifndef GLDX_VOXEL_CHUNK_H
#define GLDX_VOXEL_CHUNK_H

/**
 * @file Chunk.h
 * @brief Fixed-size voxel storage cube + the world-sampling interface the
 *        mesher and DDA raycast consume (pure CPU, no GL).
 *
 * A Chunk is a kChunkSize^3 array of block ids, anchored at an integer world
 * origin. It deliberately knows nothing about meshes or GL: the render-side
 * record (VoxelChunkGpu in VoxelMesh.h) pairs one chunk with its GPU buffers.
 *
 * IVoxelSource abstracts "what block is at this world cell" so the mesher can
 * look across chunk borders without owning a world: any object answering
 * Sample() (a chunk grid, a single chunk for local tests, a generator) works.
 *
 * Chunks are not internally synchronised: like the two-phase assets convention,
 * build/mutate one chunk on a single worker (generation/meshing job) and hand
 * completed data over; the demo keeps one owning thread per chunk job.
 */

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace gldx {

inline constexpr int kChunkSize = 16;   // cells per axis (classic 16^3 chunk)

// Address a cell inside a chunk (any axis out of range => air).
inline constexpr std::size_t VoxelIndex(int x, int y, int z) {
    return (static_cast<std::size_t>(y) * kChunkSize + static_cast<std::size_t>(z))
               * kChunkSize + static_cast<std::size_t>(x);
}

inline constexpr bool VoxelInBounds(int x, int y, int z) {
    return x >= 0 && y >= 0 && z >= 0 && x < kChunkSize && y < kChunkSize && z < kChunkSize;
}

// World-space block query used by mesher / raycast / light code.
class IVoxelSource {
public:
    virtual ~IVoxelSource() = default;
    virtual std::uint16_t Sample(glm::ivec3 worldCell) const = 0;
};

class Chunk final : public IVoxelSource {
public:
    Chunk() = default;
    explicit Chunk(glm::ivec3 origin) : origin_(origin) {}

    // Local accessors (out-of-bounds reads air, writes are dropped).
    [[nodiscard]] std::uint16_t Get(int x, int y, int z) const {
        return VoxelInBounds(x, y, z) ? blocks_[VoxelIndex(x, y, z)] : 0;
    }
    void Set(int x, int y, int z, std::uint16_t id) {
        if (!VoxelInBounds(x, y, z)) return;
        std::uint16_t& cell = blocks_[VoxelIndex(x, y, z)];
        if (cell == id) return;
        cell = id;
        dirty_ = true;
        // Border edits change the neighbour chunks' face-visibility too.
        if (x == 0)             neighborDirty_[0] = true;
        if (x == kChunkSize - 1) neighborDirty_[1] = true;
        if (y == 0)             neighborDirty_[2] = true;
        if (y == kChunkSize - 1) neighborDirty_[3] = true;
        if (z == 0)             neighborDirty_[4] = true;
        if (z == kChunkSize - 1) neighborDirty_[5] = true;
    }

    // IVoxelSource: answers cells inside this chunk only (air outside), so a
    // lone chunk is a valid source for interior meshing tests.
    [[nodiscard]] std::uint16_t Sample(glm::ivec3 worldCell) const override {
        const glm::ivec3 local = worldCell - origin_;
        return Get(local.x, local.y, local.z);
    }

    [[nodiscard]] glm::ivec3 origin() const noexcept { return origin_; }
    void SetOrigin(glm::ivec3 o) noexcept { origin_ = o; dirty_ = true; }

    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    void ClearDirty() noexcept { dirty_ = false; }
    void MarkDirty() noexcept { dirty_ = true; }

    // Which of the six neighbour sides need a remesh because this chunk's
    // border cells changed (index: 0=-x 1=+x 2=-y 3=+y 4=-z 5=+z).
    [[nodiscard]] bool neighborDirty(int side) const noexcept { return neighborDirty_[side]; }
    void ClearNeighborDirty() noexcept { neighborDirty_.fill(false); }

    [[nodiscard]] const std::array<std::uint16_t,
        static_cast<std::size_t>(kChunkSize) * kChunkSize * kChunkSize>& blocks() const noexcept {
        return blocks_;
    }

    // World-space bounds helpers for frustum culling / DDA limits.
    [[nodiscard]] glm::vec3 center() const {
        return glm::vec3(origin_) + glm::vec3(kChunkSize * 0.5f);
    }
    static constexpr float circumRadius() {
        return 0.5f * kChunkSize * 1.7320508f;   // half diagonal of the cube
    }

private:
    glm::ivec3 origin_{0, 0, 0};
    std::array<std::uint16_t,
               static_cast<std::size_t>(kChunkSize) * kChunkSize * kChunkSize> blocks_{};
    bool dirty_ = true;
    std::array<bool, 6> neighborDirty_{};
};

} // namespace gldx

#endif // GLDX_VOXEL_CHUNK_H
