#ifndef VOXEL_TERRAIN_WORLD_H
#define VOXEL_TERRAIN_WORLD_H

/**
 * @file World.h
 * @brief The voxel_terrain demo's game-side world layer: the fixed grid of
 *        chunks, the procedural terrain / tree generator, the falling-water
 *        cellular automaton, and the CPU block-texture array.
 *
 * Everything engine-side (chunks, mesher, texture array type, voxel passes, DDA
 * raycast, noise) lives in module gldx; this file is the world *policy* the demo
 * builds on top of it — the point being that the engine needs no world layer of
 * its own to render one.
 *
 * NOTE: a demo header cannot itself `import gldx`, so this file names gldx
 * engine types and must be included AFTER `import gldx;` in each .cpp.
 */

#include <array>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

namespace voxel_terrain {

// ---- world shape -----------------------------------------------------------
// 32 x 8 x 32 chunks of 16^3 = a 512 x 128 x 512 block world. Only the chunks
// near the camera are ever generated, meshed or uploaded.
constexpr int kGridX = 32;
constexpr int kGridY = 8;
constexpr int kGridZ = 32;
constexpr int kChunk = gldx::kChunkSize;                       // 16
constexpr int kWorldX = kGridX * kChunk;                      // 512
constexpr int kWorldY = kGridY * kChunk;                      // 128
constexpr int kWorldZ = kGridZ * kChunk;                      // 512
constexpr int kSeaLevel = 30;
constexpr int kRenderDistance = 5;      // chunks, horizontally
constexpr std::size_t kMaxGenInFlight = 64;
constexpr std::size_t kMaxMeshInFlight = 16;
constexpr std::size_t kMaxUploadsPerFrame = 6;
constexpr float kReach = 6.0f;          // block interaction distance (blocks)
constexpr float kEyeHeight = 1.62f;     // fly camera eye above the feet (blocks)
constexpr double kWaterTick = 1.0 / 8.0; // seconds between water-flow steps

using Block = gldx::BlockRegistry;   // home of the built-in block ids
using BlockId = std::uint16_t;

// A handful of cheap integer hashes: terrain, tree placement and the texture
// jitter must be reproducible on every platform, so they are derived from
// integers only (no FP-order dependence).
inline std::uint32_t Hash2(int x, int z) {
    std::uint32_t h = 0x9e3779b9u;
    h ^= static_cast<std::uint32_t>(x) * 0x85ebca6bu;
    h ^= static_cast<std::uint32_t>(z) * 0xc2b2ae35u;
    h ^= h >> 13;
    h *= 0x27d4eb2fu;
    h ^= h >> 16;
    return h;
}

// Terrain column height (number of solid cells from y=0 upward). Tuned so the
// sea level (30) carves real coastlines.
int TerrainHeight(const gldx::Noise& noise, int wx, int wz);

bool TreeAnchor(int wx, int wz);

// Top material of a column: beaches near the water line, snow on the peaks.
BlockId SurfaceBlock(int topY);

// Debris colour per block type.
glm::vec3 BlockTint(BlockId id);

// Fog and sky colours are authored like sRGB hues but mixed in the linear HDR
// target, so they get the same approximate-gamma conversion the textures do.
glm::vec3 ToLinear(const glm::vec3& srgb);

// ---- procedural block texture array ----------------------------------------
// One 16x16 slice per block type (texLayer 1..8; slice 0 stays unused because
// id 0 is air). Layer isolation is the whole reason for the array.
gldx::Texture2DArrayDesc MakeBlockAtlasDesc();

// ---- the chunk grid (game side; deliberately outside the engine) ------------
// Streaming state of one grid cell. kVoid is the cached answer to "this cell
// can never hold terrain" (pure air above the surface).
enum Phase : std::uint8_t {
    kEmpty = 0, kGenerating, kGenerated, kMeshing, kReady, kVoid,
};

struct ChunkState {
    std::uint8_t phase = kEmpty;
};

struct World final : gldx::IVoxelSource {
    std::vector<std::unique_ptr<gldx::Chunk>> cpu;   // filled lazily
    std::vector<gldx::VoxelChunkGpu> gpu;            // one record per grid cell
    std::vector<ChunkState> state;
    const gldx::BlockRegistry blocks;
    const gldx::ChunkMesher mesher{blocks};
    const gldx::Noise noise{0x5EED1u};
    mutable std::shared_mutex mx;                   // guards cpu block data

    World()
        : cpu(static_cast<std::size_t>(kGridX) * kGridY * kGridZ)
        , gpu(static_cast<std::size_t>(kGridX) * kGridY * kGridZ)
        , state(static_cast<std::size_t>(kGridX) * kGridY * kGridZ) {}

    static int Index(int cx, int cy, int cz) { return (cy * kGridZ + cz) * kGridX + cx; }
    static bool InRange(int cx, int cy, int cz) {
        return cx >= 0 && cy >= 0 && cz >= 0 && cx < kGridX && cy < kGridY && cz < kGridZ;
    }

    // gldx::IVoxelSource. Callers hold at least a shared lock (the meshing jobs
    // take one for their whole Build, so nested Sample calls never re-lock).
    std::uint16_t Sample(glm::ivec3 c) const override {
        if (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= kWorldX || c.y >= kWorldY || c.z >= kWorldZ)
            return 0;
        const gldx::Chunk* ch = cpu[Index(c.x / kChunk, c.y / kChunk, c.z / kChunk)].get();
        return ch ? ch->Get(c.x & (kChunk - 1), c.y & (kChunk - 1), c.z & (kChunk - 1)) : 0;
    }

    // Blocks the crosshair ray: everything but air and water.
    bool Pickable(glm::ivec3 c) const {
        const BlockId id = Sample(c);
        return id != 0 && id != Block::kWater;
    }

    // Chunk coordinates of the camera, refreshed by the streaming loop each
    // frame. Render-thread writes only; the meshing jobs never read it.
    glm::ivec2 camChunk{0, 0};

    // Is this grid column inside the ring currently being streamed?
    [[nodiscard]] bool InRing(int cx, int cz) const {
        const int dx = cx - camChunk.x, dz = cz - camChunk.y;
        return dx * dx + dz * dz <= kRenderDistance * kRenderDistance;
    }

    [[nodiscard]] bool NeighborReady(int cx, int cy, int cz) const {
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = cx + dx, ny = cy + dy, nz = cz + dz;
                    if (!InRange(nx, ny, nz)) continue;
                    if (!InRing(nx, nz)) continue;
                    const int nidx = Index(nx, ny, nz);
                    if (state[nidx].phase == kVoid) continue;
                    if (!cpu[nidx]) return false;
                }
        return true;
    }

    // Cheap pre-filter for the streaming loop: does this grid cell hold
    // anything at all? Pure air above the terrain (and above the water line)
    // never needs a mesh or an upload.
    [[nodiscard]] bool ColumnRelevant(int cx, int cy, int cz) const {
        constexpr int kMargin = 12;      // blocks of slack above the sampled h
        const int y0 = cy * kChunk;
        for (int lz = 0; lz < kChunk; lz += 4) {
            for (int lx = 0; lx < kChunk; lx += 4) {
                const int h = TerrainHeight(noise, cx * kChunk + lx, cz * kChunk + lz);
                if (y0 < h + kMargin
                    || (y0 < kSeaLevel && kSeaLevel <= y0 + kChunk)) return true;
            }
        }
        return false;
    }

    // Apply one block edit (air => break). Marks the owning chunk dirty, and
    // border edits flag the neighbour side, which the mesh drain turns into the
    // neighbour's own remesh.
    bool Edit(glm::ivec3 cell, BlockId id) {
        if (cell.x < 0 || cell.y < 0 || cell.z < 0
            || cell.x >= kWorldX || cell.y >= kWorldY || cell.z >= kWorldZ) return false;
        const int cx = cell.x / kChunk, cy = cell.y / kChunk, cz = cell.z / kChunk;
        std::unique_lock<std::shared_mutex> lk(mx);
        gldx::Chunk* ch = cpu[Index(cx, cy, cz)].get();
        if (!ch) return false;
        ch->Set(cell.x & (kChunk - 1), cell.y & (kChunk - 1), cell.z & (kChunk - 1), id);
        return true;
    }
};

// A deliberately small falling-water update for the demo: water with air
// directly beneath it descends one cell per tick (volume is conserved), and
// cells never spread sideways or rise. Driven from a per-edit work set on the
// render thread, so a change costs a bounded number of cell reads.
struct WaterSim final {
    static int Pack(glm::ivec3 c) { return (c.y * kWorldZ + c.z) * kWorldX + c.x; }
    static glm::ivec3 Unpack(int k) {
        const int x = k % kWorldX; k /= kWorldX;
        const int z = k % kWorldZ; k /= kWorldZ;
        return {x, k, z};
    }

    // Queue a cell, its six neighbours and the one above for the next step.
    void schedule(glm::ivec3 c) {
        insert(c);
        insert(c + glm::ivec3(0, 1, 0));
        insert(c + glm::ivec3(0, -1, 0));
        insert(c + glm::ivec3(1, 0, 0));
        insert(c + glm::ivec3(-1, 0, 0));
        insert(c + glm::ivec3(0, 0, 1));
        insert(c + glm::ivec3(0, 0, -1));
    }

    // Advance by dt seconds, stepping at most once per kWaterTick.
    void step(World& world, float dt);

private:
    void insert(glm::ivec3 c) {
        if (c.x < 0 || c.y < 0 || c.z < 0
            || c.x >= kWorldX || c.y >= kWorldY || c.z >= kWorldZ) return;
        active.insert(Pack(c));
    }
    std::unordered_set<int> active;
    double timer = 0.0;
};

// Fill one chunk: terrain columns + trees. Pure function of the grid coord, so
// any worker can run it for any chunk without talking to anybody else. Returns
// false for a chunk that came out completely empty.
bool GenerateChunk(gldx::Chunk& ch, const World& world);

} // namespace voxel_terrain

#endif // VOXEL_TERRAIN_WORLD_H
