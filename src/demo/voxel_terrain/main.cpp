/**
 * @file voxel_main.cpp
 * @brief voxel_demo — the playable acceptance test for gldx's voxel primitives.
 *
 * Everything engine-side lives in module gldx (chunks, the mesher, the texture
 * array, the voxel passes, the DDA raycast, noise, particles, fog). What is
 * *game* side — the chunk grid, the streaming policy, the terrain generator,
 * the block editing rules and the HUD — is built here, on top of the library,
 * which is the point: the engine must not need a world layer of its own to
 * render one.
 *
 * Pipeline (a custom pass order rather than the default one):
 *   VoxelOpaquePass -> VoxelTransparentPass -> PostProcessPass -> VoxelHudPass
 *
 * Threading follows the engine's two-phase convention: worker threads run the
 * terrain generation and the chunk meshing (pure CPU, `ChunkMesher`), and the
 * render thread drains the completed jobs and uploads them, a few per frame,
 * so a world edit or a fast flight never stalls the frame. Workers read block
 * data under a shared lock; edits take the exclusive lock (see World::mx).
 *
 * Controls:
 *   mouse        : look around (pointer captured)
 *   W/A/S/D      : fly forward / back / left / right
 *   Space / Ctrl : fly up / down          hold LeftShift : slower flight
 *   left click   : break the aimed block (debris particles)
 *   right click  : place the selected block on the aimed face
 *   1..8         : choose the block type
 *   F            : toggle fly / orbit camera (ESC releases the pointer, or
 *                  quits while orbiting)       X : quit
 *   [ / ]        : sun azimuth                - / = : sun elevation
 *   P            : toggle the debris particles
 *   B            : toggle two-sided terrain (skips back-face culling, so the
 *                  far walls of a dig stay visible when you look from below)
 *   Tab          : toggle perspective / orthographic projection
 *   scroll       : zoom the orbit camera (orbit mode)
 *
 * Every renderer feature can also be switched from the command line, which is
 * what lets a script diff one feature at a time without a keyboard: see
 * demo_cli.h and `--help` for the list (--off fog,water,sky,particles,
 * --on ortho, --auto-break N, --quit-after SECONDS). --yaw / --pitch / --rise
 * aim and lift the fly camera: at the spawn tilt the crosshair ray lands past
 * the interaction reach, so scripted mining needs a steeper pitch, and
 * --auto-place N with --select 7 builds the water a water test cannot find on
 * its own in this part of the world. --on double-sided mirrors the B key. The
 * fly camera now collides with terrain (a dug shaft can be descended to its
 * floor, and water above a broken cell pours down to fill it). --freeze-at also
 * leaves the pointer alone:
 * a captured pointer lets whoever is moving the mouse next to the window rewrite
 * --yaw / --pitch, and the scripted edit counts with it.
 *
 * The window is created with the same 4.1-core hints as the PBR demo; see
 * main.cpp for the engine's other showcase.
 */

// Platform.h stays a plain text include (not part of module gldx): it orders
// <glad/gl.h> before <GLFW/glfw3.h> and carries the window constants.
#include "gldx/core/Platform.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <deque>
#include <future>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "demo/demo_cli.h"

// The whole engine as a single C++20 named module.
import gldx;

namespace {

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
// sea level (30) carves real coastlines: about a third of the map ends up
// underwater, the rest is grass plain, and the ridge term alone reaches the
// snow line.
int TerrainHeight(const gldx::Noise& noise, int wx, int wz) {
    const double base   = noise.Fbm2(wx / 96.0,  wz / 96.0,  4);   // rolling hills
    const double detail = noise.Fbm2(wx / 24.0,  wz / 24.0,  3);   // bumps
    const double mask   = noise.Fbm2(wx / 240.0, wz / 240.0, 2);   // where mountains sit
    double h = 24.0 + 26.0 * gldx::Noise::ToUnit(base) + 6.0 * detail;
    const double lift = std::max(0.0, mask - 0.15);
    h += 150.0 * lift * lift;        // steep, rare ridges
    return static_cast<int>(std::clamp(h, 3.0, static_cast<double>(kWorldY - 12)));
}

bool TreeAnchor(int wx, int wz) { return Hash2(wx, wz) % 977u < 5u; }

using Block = gldx::BlockRegistry;   // home of the built-in block ids
using BlockId = std::uint16_t;

// Top material of a column: beaches near the water line, snow on the peaks.
BlockId SurfaceBlock(int topY) {
    if (topY <= kSeaLevel + 1) return Block::kSand;
    if (topY > 78) return Block::kSnow;
    return Block::kGrass;
}

// Debris colour per block type. The particle batch samples no atlas, so the
// puff is tinted from the same palette the texture slices are drawn from.
glm::vec3 BlockTint(BlockId id) {
    switch (id) {
        case Block::kGrass:  return {0.26f, 0.60f, 0.22f};
        case Block::kDirt:   return {0.44f, 0.30f, 0.19f};
        case Block::kStone:  return {0.48f, 0.48f, 0.51f};
        case Block::kSand:   return {0.83f, 0.77f, 0.52f};
        case Block::kWood:   return {0.37f, 0.26f, 0.15f};
        case Block::kLeaves: return {0.16f, 0.42f, 0.14f};
        case Block::kWater:  return {0.17f, 0.40f, 0.76f};
        case Block::kSnow:   return {0.92f, 0.95f, 1.00f};
        default:             return {0.50f, 0.50f, 0.50f};
    }
}

// Fog and sky colours are authored like sRGB hues but mixed in the linear HDR
// target, so they get the same approximate-gamma conversion the textures do.
glm::vec3 ToLinear(const glm::vec3& srgb) {
    return {static_cast<float>(std::pow(srgb.r, 2.2)),
            static_cast<float>(std::pow(srgb.g, 2.2)),
            static_cast<float>(std::pow(srgb.b, 2.2))};
}

// ---- procedural block texture array ----------------------------------------
// One 16x16 slice per block type (texLayer 1..8; slice 0 stays unused because
// id 0 is air). Layer isolation is the whole reason for the array: no bleed,
// free per-slice mipmapping, and the mesher only carries a layer float.
gldx::Texture2DArrayDesc MakeBlockAtlasDesc() {
    constexpr int kTile = 16;
    constexpr int kLayers = 9;
    gldx::Texture2DArrayDesc d;
    d.width = d.height = kTile;
    d.layers = kLayers;
    d.channels = 4;
    d.srgb = true;
    d.pixels.assign(static_cast<std::size_t>(kLayers) * kTile * kTile * 4, 255);

    for (int layer = 0; layer < kLayers; ++layer) {
        for (int y = 0; y < kTile; ++y) {
            for (int x = 0; x < kTile; ++x) {
                const std::size_t off =
                    (static_cast<std::size_t>(layer) * kTile * kTile + y * kTile + x) * 4;
                // -8%..+8% value jitter, deterministic per texel.
                const float n = (static_cast<float>(Hash2(x + layer * 31, y * 7 + layer) & 4095)
                                 / 4095.0f - 0.5f) * 0.16f;
                glm::vec3 c{1.0f};
                float a = 1.0f;
                switch (layer) {
                    case Block::kGrass:  c = {0.26f + n, 0.60f + n * 1.4f, 0.22f + n}; break;
                    case Block::kDirt:   c = {0.44f + n, 0.30f + n, 0.19f + n}; break;
                    case Block::kStone:  c = {0.48f + n, 0.48f + n, 0.51f + n}; break;
                    case Block::kSand:   c = {0.83f + n, 0.77f + n, 0.52f + n}; break;
                    case Block::kWood: {
                        // Vertical bark stripes.
                        const float s = ((x % 4) < 2) ? 0.0f : -0.09f;
                        c = {0.37f + s + n, 0.26f + s + n, 0.15f + s * 0.5f + n};
                        break;
                    }
                    case Block::kLeaves: {
                        c = {0.16f + n, 0.42f + n * 1.5f, 0.14f + n};
                        // Sparse holes: the cutout discard turns them into gaps.
                        const std::uint32_t h = Hash2(x * 3 + 11, y * 5 + layer);
                        a = ((h & 15u) == 0u) ? 0.0f : 1.0f;
                        break;
                    }
                    case Block::kWater:  c = {0.17f + n * 0.5f, 0.40f + n, 0.76f + n}; a = 0.72f; break;
                    case Block::kSnow:   c = {0.92f + n * 0.3f, 0.95f, 1.0f}; break;
                    default:             c = {0.5f, 0.0f, 0.5f}; break;   // slice 0: unused
                }
                for (int ch = 0; ch < 3; ++ch) {
                    const float v = (ch == 0 ? c.r : (ch == 1 ? c.g : c.b)) * (1.0f + n * 0.25f);
                    d.pixels[off + ch] = static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
                }
                d.pixels[off + 3] = static_cast<std::uint8_t>(a * 255.0f);
            }
        }
    }
    return d;
}

// ---- the chunk grid (game side; deliberately outside the engine) ------------
// Streaming state of one grid cell. kVoid is the cached answer to "this cell
// can never hold terrain" (pure air above the surface): the height test costs
// a few noise lookups, so it runs once per cell, and a void cell counts as a
// known-air neighbour - which is what stops border chunks waiting forever.
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

    // Blocks the crosshair ray: everything but air and water. Water still is not
    // minable, but it now flows: WaterSim drops water into any air dug beneath
    // it, so opening a hole under a lake fills the hole rather than leaving the
    // water hanging in mid-air.
    bool Pickable(glm::ivec3 c) const {
        const BlockId id = Sample(c);
        return id != 0 && id != Block::kWater;
    }

    // Chunk coordinates of the camera, refreshed by the streaming loop each
    // frame. Render-thread writes only; the meshing jobs never read it.
    glm::ivec2 camChunk{0, 0};

    // Is this grid column inside the ring currently being streamed? The mirror
    // of the candidate filter, so "never generated" and "not yet generated"
    // can be told apart (see NeighborReady).
    [[nodiscard]] bool InRing(int cx, int cz) const {
        const int dx = cx - camChunk.x, dz = cz - camChunk.y;
        return dx * dx + dz * dz <= kRenderDistance * kRenderDistance;
    }

    [[nodiscard]] bool NeighborReady(int cx, int cy, int cz) const {
        for (int dz = -1; dz <= 1; ++dz)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = cx + dx, ny = cy + dy, nz = cz + dz;
                    // Outside the world is void and always "known"; outside the
                    // streamed ring nothing will ever be generated, so meshing
                    // on the assumption of air there is the only way the ring
                    // edge can finish at all - and it is hidden by the fog.
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
    //
    // The height field is sampled on a 4-cell lattice, so the test must err
    // towards keeping: a peak can sit between the samples, and the shortest
    // term in TerrainHeight moves the surface by more than the sample spacing.
    // A false keep costs one worker pass that GenerateChunk then reports as
    // empty; a false drop would punch a 16 x 16 hole in the world.
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

// A deliberately small falling-water update for the demo. The mesher and block
// registry already know water is transparent, but nothing in the library moves
// it, so before this a lake resting on a just-mined block hung in mid-air. The
// rule set is the gravity half of a cellular fluid and nothing more: water with
// air directly beneath it descends one cell per tick (volume is conserved, so
// the surface recedes as a shaft fills), and cells never spread sideways or rise.
// That answers "I dug under the water, why didn't it fall?" without the flow
// levels / source bookkeeping a full simulation needs. It is driven from a
// per-edit work set on the render thread, so a change costs a bounded number of
// cell reads instead of a whole-world scan.
struct WaterSim final {
    // Pack a world cell into a work-set key. The grid is far under 2^31, so one
    // int suffices and keeps the set cheap.
    static int Pack(glm::ivec3 c) { return (c.y * kWorldZ + c.z) * kWorldX + c.x; }
    static glm::ivec3 Unpack(int k) {
        const int x = k % kWorldX; k /= kWorldX;
        const int z = k % kWorldZ; k /= kWorldZ;
        return {x, k, z};
    }

    // Queue a cell, its six neighbours and the one above (the usual feeder) for
    // the next step. Called after any block edit; cells that are neither water
    // nor sitting under water do nothing when evaluated.
    void schedule(glm::ivec3 c) {
        insert(c);
        insert(c + glm::ivec3(0, 1, 0));
        insert(c + glm::ivec3(0, -1, 0));
        insert(c + glm::ivec3(1, 0, 0));
        insert(c + glm::ivec3(-1, 0, 0));
        insert(c + glm::ivec3(0, 0, 1));
        insert(c + glm::ivec3(0, 0, -1));
    }

    // Advance by dt seconds, stepping at most once per kWaterTick so a pour reads
    // as falling liquid rather than a teleport. Reads run under a shared lock and
    // are applied afterwards (World::Edit takes the exclusive lock itself). A
    // falling source and a fall target can never collide in one tick: sources are
    // water (their below is air), targets are air, and "below" is injective.
    void step(World& world, float dt) {
        timer += dt;
        if (timer < kWaterTick) return;
        timer = 0.0;
        if (active.empty()) return;
        std::unordered_set<int> cur;
        cur.swap(active);

        std::vector<std::pair<glm::ivec3, glm::ivec3>> moves;
        {
            std::shared_lock<std::shared_mutex> lk(world.mx);
            for (int key : cur) {
                const glm::ivec3 c = Unpack(key);
                if (world.Sample(c) != Block::kWater) continue;
                const glm::ivec3 below = c - glm::ivec3(0, 1, 0);
                if (below.y < 0) continue;                        // world floor: nowhere to fall
                if (world.Sample(below) == 0) moves.emplace_back(c, below);
            }
        }
        for (const auto& [src, dst] : moves) {
            world.Edit(dst, Block::kWater);
            world.Edit(src, 0);
            // Keep the stream going: the destination may itself have air below,
            // and the now-empty source should pull whatever feeds it from above.
            schedule(dst);
            schedule(src + glm::ivec3(0, 1, 0));
        }
    }

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
// any worker can run it for any chunk without talking to anybody else. Trees
// are stamped with a 2-cell margin, which clips canopies at chunk borders
// instead of requiring cross-chunk writes. Returns false for a chunk that came
// out completely empty, which is how the streaming loop turns a false keep from
// the cheap pre-filter into a kVoid cell.
bool GenerateChunk(gldx::Chunk& ch, const World& world) {
    const glm::ivec3 o = ch.origin();

    for (int lz = 0; lz < kChunk; ++lz) {
        for (int lx = 0; lx < kChunk; ++lx) {
            const int wx = o.x + lx, wz = o.z + lz;
            const int h = TerrainHeight(world.noise, wx, wz);
            const BlockId surface = SurfaceBlock(h);
            for (int ly = 0; ly < kChunk; ++ly) {
                const int wy = o.y + ly;
                BlockId id = 0;
                if (wy < h) {
                    if (wy < h - 4)     id = Block::kStone;
                    else if (wy < h - 1) id = Block::kDirt;
                    else                 id = surface;
                } else if (wy < kSeaLevel) {
                    id = Block::kWater;
                }
                if (id) ch.Set(lx, ly, lz, id);
            }
        }
    }

    // Tree band: trunks start just above the water line and the tallest canopy
    // tops out near y = 88, so a chunk outside that vertical range can never
    // receive a cell and skips the anchor scan entirely.
    if (o.y + kChunk > kSeaLevel + 2 && o.y < 90) {
        // A canopy reaches 2 cells sideways and the trunk ~7 up, so anchors
        // within that margin of the chunk can still put cells inside it.
        for (int tz = -2; tz < kChunk + 2; ++tz) {
            for (int tx = -2; tx < kChunk + 2; ++tx) {
                const int wx = o.x + tx, wz = o.z + tz;
                if (!TreeAnchor(wx, wz)) continue;
                const int ground = TerrainHeight(world.noise, wx, wz);
                if (ground <= kSeaLevel + 1 || ground > 76) continue;
                const int trunk = 4 + static_cast<int>(Hash2(wx, wz) % 3u);

                auto put = [&](int ax, int ay, int az, BlockId id, bool replaceOnlyAir) {
                    if (ax < 0 || az < 0 || ay < 0 || ax >= kChunk || az >= kChunk
                        || ay >= kChunk) return;
                    if (replaceOnlyAir && ch.Get(ax, ay, az) != 0) return;
                    ch.Set(ax, ay, az, id);
                };

                for (int i = 0; i < trunk; ++i)
                    put(tx, ground + i - o.y, tz, Block::kWood, false);
                const int top = ground + trunk;
                for (int dy = -2; dy <= 1; ++dy) {
                    const int r = (dy >= 1) ? 1 : 2;
                    for (int dz = -r; dz <= r; ++dz)
                        for (int dx = -r; dx <= r; ++dx) {
                            if (dy == 1 && (std::abs(dx) + std::abs(dz)) > 2) continue;
                            if (std::abs(dx) == r && std::abs(dz) == r && dy < 1) continue;
                            put(tx + dx, top + dy - o.y, tz + dz, Block::kLeaves, true);
                        }
                }
            }
        }
    }

    // Border writes during *generation* are the chunk's own content, not an
    // edit into a loaded neighbour; drop those flags so the first mesh does not
    // cascade a remesh through the whole neighbourhood.
    ch.ClearNeighborDirty();

    const auto& cells = ch.blocks();
    return std::any_of(cells.begin(), cells.end(),
                       [](std::uint16_t id) { return id != 0; });
}

// ---- input -----------------------------------------------------------------
struct Input {
    enum class Cam { Fly, Orbit };
    Cam cam = Cam::Fly;
    bool captured = true;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    // Fly orientation (rad). A positive pitch looks up in the engine's
    // convention, so the spawn tilt is negative: the terrain, not the sky.
    float yaw = 0.7f, pitch = -0.18f;
    float orbitYaw = 0.7f, orbitPitch = 0.35f;
    float orbitRadius = 26.0f;
    glm::vec3 focus{0.0f};                        // orbit pivot / fly position
    float speed = 24.0f;
    bool ortho = false;                           // Tab: projection toggle
    int selected = Block::kGrass;
    float sunAzimuth = 0.75f, sunElevation = 0.8f;
    bool wantBreak = false, wantPlace = false;
    bool showParticles = true;
    // Renderer features that --off can switch off before the first frame.
    bool showSky = true;
    bool showFog = true;
    float waterAlpha = 0.85f;
    bool doubleSided = false;                 // B: draw terrain two-sided (no back cull)
};

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

glm::vec3 SunToward(const Input& in) {
    const float ce = std::cos(in.sunElevation);
    return glm::normalize(glm::vec3(ce * std::sin(in.sunAzimuth),
                                    std::sin(in.sunElevation),
                                    ce * std::cos(in.sunAzimuth)));
}

void ApplyCapture(GLFWwindow* win, Input& in, bool keepCursor = false) {
    // `keepCursor` is the scripted-run override: with --freeze-at the view has to
    // stay where --yaw/--pitch put it, and a captured pointer hands the last word
    // to whoever is moving the mouse next to the window.
    in.captured = !keepCursor && (in.cam == Input::Cam::Fly);
    glfwSetInputMode(win, GLFW_CURSOR,
                     in.captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void MouseCallback(GLFWwindow* win, double x, double y) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    const float dx = static_cast<float>(x - in->lastX);
    const float dy = static_cast<float>(y - in->lastY);
    in->lastX = x;
    in->lastY = y;
    if (in->captured) {
        // FPS look: right on the mouse moves the view right (positive yaw turns
        // left in the engine's convention), down moves it down.
        in->yaw   -= dx * 0.0032f;
        in->pitch -= dy * 0.0032f;
        in->pitch = std::clamp(in->pitch, -1.5f, 1.5f);
        return;
    }
    if (!in->dragging) return;
    in->orbitYaw   -= dx * 0.006f;
    in->orbitPitch = std::clamp(in->orbitPitch + dy * 0.006f, -1.45f, 1.45f);
}

void MouseButtonCallback(GLFWwindow* win, int button, int action, int) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in || action != GLFW_PRESS) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (in->cam == Input::Cam::Fly) in->wantBreak = true;
        else { in->dragging = true; double cx = 0, cy = 0; glfwGetCursorPos(win, &cx, &cy);
               in->lastX = cx; in->lastY = cy; }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (in->cam == Input::Cam::Fly) in->wantPlace = true;
    }
}

void ScrollCallback(GLFWwindow* win, double, double dy) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    in->orbitRadius = std::clamp(in->orbitRadius - static_cast<float>(dy) * 1.2f, 6.0f, 120.0f);
}

// The HUD pass: reuses the engine's sprite + text renderers, then closes the
// frame in the profiler. Demo-owned on purpose - the library's DebugHudPass
// prints the PBR demo's scene-graph counters, which say nothing here.
class VoxelHudPass final : public gldx::RenderPass {
public:
    VoxelHudPass() : gldx::RenderPass("VoxelHud") {}

    void Execute(gldx::RenderFrame& f) override;

    std::string text;
    bool crosshair = false;
};

void VoxelHudPass::Execute(gldx::RenderFrame& f) {
    gldx::RenderContext::AssertRenderThread("VoxelHudPass::Execute");
    if (f.overlay.sprite && f.overlay.font && f.overlay.white) {
        f.overlay.sprite->Begin(*f.overlay.white, f.fbWidth, f.fbHeight);
        f.overlay.sprite->Draw(*f.overlay.white, 0.0f, 0.0f, 560.0f, 116.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                       glm::vec4(0.0f, 0.0f, 0.0f, 0.35f));
        gldx::TextRenderer::Draw(*f.overlay.sprite, *f.overlay.font, text, 12.0f, 8.0f, 20.0f,
                                glm::vec4(1.0f));
        if (crosshair) {
            const float cx = f.fbWidth * 0.5f, cy = f.fbHeight * 0.5f;
            f.overlay.sprite->Draw(*f.overlay.white, cx - 8.0f, cy - 1.0f, 16.0f, 2.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                           glm::vec4(1.0f, 1.0f, 1.0f, 0.75f));
            f.overlay.sprite->Draw(*f.overlay.white, cx - 1.0f, cy - 8.0f, 2.0f, 16.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                           glm::vec4(1.0f, 1.0f, 1.0f, 0.75f));
        }
        f.overlay.sprite->End();
    }
    if (f.overlay.profiler) f.overlay.profiler->EndFrame();
}

} // namespace

int main(int argc, char** argv) {
    const demo::Flags flags(argc, argv,
                            {"particles", "fog", "water", "sky", "ortho", "double-sided"},
                            {"auto-break", "auto-place", "freeze-at", "yaw", "pitch", "rise",
                             "select"}, "voxel_terrain");
    if (flags.wantsHelp()) {
        flags.printUsage();
        return 0;
    }

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if GLFW_PLATFORM_MACOS
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(gldx::kWindowWidth, gldx::kWindowHeight,
                                          "gldx::Renderer - voxel playground",
                                          nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window (OpenGL 4.1 core?)\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    gldx::RenderContext::MarkAsRenderThread();
    std::printf("voxel_demo %s\n", gldx::kAppVersion);
    std::printf("OpenGL %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)));

    Input input;
    input.showParticles = flags.on("particles");
    input.showFog = flags.on("fog");
    input.showSky = flags.on("sky");
    // The water sheet is alpha-blended, so "off" means fully transparent rather
    // than a pass removed from the pipeline: the pass also draws the particles,
    // and the two switches have to stay independent.
    input.waterAlpha = flags.on("water") ? 0.85f : 0.0f;
    input.ortho = flags.on("ortho", false);
    input.doubleSided = flags.on("double-sided", false);
    // Which block --auto-place builds with; the sweep uses 7 (water) to put a
    // transparent sheet in the frame without needing a lake to be nearby.
    input.selected = std::clamp(flags.integer("select", input.selected),
                                1, static_cast<int>(Block::kBuiltinCount) - 1);
    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, MouseCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetScrollCallback(window, ScrollCallback);

    // Same teardown contract as main.cpp: everything owning GL lives in this
    // lambda so destructors run while the context is current.
    auto runDemo = [&]() -> int {
        // ---- engine objects -------------------------------------------------
        gldx::Renderer renderer;
        renderer.Init();
        // No BuildPbrPipeline(): the voxel demo swaps GeometryPass for the
        // voxel pair, adds the standalone sky stage between opaque and
        // transparent (where the inline sky used to sit), and brings its own HUD.
        auto opaquePass = std::make_unique<gldx::VoxelOpaquePass>();
        auto transpPass = std::make_unique<gldx::VoxelTransparentPass>();
        auto hudPass = std::make_unique<VoxelHudPass>();
        gldx::VoxelOpaquePass* opaqueRaw = opaquePass.get();
        gldx::VoxelTransparentPass* transpRaw = transpPass.get();
        VoxelHudPass* hudRaw = hudPass.get();

        renderer.AddPass(std::move(opaquePass));
        renderer.AddPass(std::make_unique<gldx::SkyboxPass>());
        renderer.AddPass(std::move(transpPass));
        renderer.AddPass(std::make_unique<gldx::PostProcessPass>());
        renderer.AddPass(std::move(hudPass));

        auto voxelProg = gldx::ShaderProgram::CreateFromSource(gldx::shaders::kVoxelVertex,
                                                             gldx::shaders::kVoxelFragment);
        if (!voxelProg) {
            std::fprintf(stderr, "Voxel shader error:\n%s\n", voxelProg.error().c_str());
            return 1;
        }
        voxelProg->Use();
        voxelProg->SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);
        // Sampler-unit uniforms persist on the program object.
        voxelProg->Set("uAtlas", static_cast<int>(gldx::texunit::voxelAtlas));

        gldx::Texture2DArray atlas;
        atlas.Upload(MakeBlockAtlasDesc());

        gldx::SkyboxRenderer skybox;
        if (!skybox.Init()) {
            std::fprintf(stderr, "Skybox init failed\n");
            return 1;
        }
        // Procedural HDR sky + IBL, baked once from the initial sun (same
        // contract as main.cpp): [ ] and - = move the direct light, not the sun
        // disc in the sky - regenerating the cube per frame would cost more than
        // the feature is worth in a demo.
        const glm::vec3 initialTravel = -SunToward(input);
        gldx::EnvironmentMap env;
        if (!env.Generate(initialTravel, 256, 32, 256)) {
            std::fprintf(stderr, "Environment generation failed\n");
            return 1;
        }

        gldx::PostProcessChain post;
        if (!post.Init()) {
            std::fprintf(stderr, "PostProcessChain init failed\n");
            return 1;
        }
        post.SetExposure(1.15f);

        gldx::SpriteBatch sprite;
        if (!sprite.Init()) {
            std::fprintf(stderr, "SpriteBatch init failed\n");
            return 1;
        }
        gldx::Font font;
        for (const char* candidate : kFontCandidates) {
            if (font.LoadFromFile(candidate, 48.0f)) break;
        }
        if (!font.loaded()) std::fprintf(stderr, "HUD font not found; text overlay disabled\n");
        gldx::Texture2D white;
        white.Upload([]() {
            gldx::Texture2DDesc d;
            d.width = d.height = 1;
            d.channels = 4;
            d.srgb = false;
            d.pixels = {255, 255, 255, 255};
            return d;
        }());

        gldx::ParticleBatch particles;
        if (!particles.Init()) std::fprintf(stderr, "ParticleBatch init failed\n");

        gldx::Profiler profiler;
        profiler.Init();

        gldx::LightBuffer lightBuffer;
        lightBuffer.Init();

        gldx::Camera camera;
        camera.SetPerspective(60.0f, 1.0f, 0.1f, 400.0f);
        // --on ortho gives the camera the one kick the Tab key would have; the
        // camera stays the projection authority and input.ortho mirrors it.
        if (input.ortho) camera.ToggleProjection();

        // ---- world + streaming state ---------------------------------------
        World world;
        WaterSim water;
        {
            // Spawn above the terrain at the world's centre. The fly camera is
            // the position authority, so the seed has to be pushed into it -
            // starting at the origin instead drops the player into the corner
            // of the world box, where the surface check then lifts them over
            // an empty chunk wall.
            const int sx = kWorldX / 2, sz = kWorldZ / 2;
            const int h = TerrainHeight(world.noise, sx, sz);
            // --rise is bounded by the world height, which also keeps the sum
            // below inside the int range however absurd the argument is. It may
            // go negative: the sweep starts one block lower than the default
            // spawn to keep the freshly dug shaft centred in the frame.
            const int rise = std::clamp(flags.integer("rise"), -kWorldY, kWorldY);
            input.focus = glm::vec3(
                static_cast<float>(sx) + 0.5f,
                static_cast<float>(std::max(h, kSeaLevel) + 6 + rise),
                static_cast<float>(sz) + 0.5f);
            input.yaw   = flags.real("yaw", input.yaw);
            input.pitch = flags.real("pitch", input.pitch);
            camera.SetYawPitch(input.yaw, input.pitch);
            camera.Translate(input.focus - camera.Position());
        }

        gldx::VoxelPipeline vx;
        vx.prog = &*voxelProg;
        vx.atlas = &atlas;
        vx.chunks = &world.gpu;
        // The atlas slice is already 0.72 alpha; the pass multiplies, so 0.85
        // lands the water sheet near two-thirds opacity - enough to read as
        // liquid while still showing the sand bottom through it. --off water
        // brings that down to 0 (see the input setup above).
        vx.waterAlpha = input.waterAlpha;
        opaqueRaw->SetPipeline(vx);
        transpRaw->SetPipeline(vx);

        gldx::ThreadPool pool(4);
        using GenResult = std::pair<std::unique_ptr<gldx::Chunk>, bool>;
        struct GenJob { int idx; std::future<GenResult> fut; };
        struct MeshJob { int idx; std::future<gldx::VoxelChunkMesh> fut; };
        std::deque<GenJob> genPending;
        std::deque<MeshJob> meshPending;
        std::vector<std::pair<int, glm::ivec3>> candidates;   // scratch
        int genCount = 0, meshCount = 0;
        // Chunk-grid side offsets, index = Chunk::neighborDirty() side.
        const glm::ivec3 kSide[6] = { {-1,0,0}, {1,0,0}, {0,-1,0},
                                      {0,1,0}, {0,0,-1}, {0,0,1} };

        auto submitMesh = [&](int idx) {
            {
                // Clear the flag *before* the job runs. An edit that lands
                // while the worker is copying then re-dirties the chunk and
                // earns a second remesh, so no change can be swallowed by a
                // mesh that was built from an older snapshot.
                std::unique_lock<std::shared_mutex> lk(world.mx);
                world.cpu[idx]->ClearDirty();
            }
            world.state[idx].phase = kMeshing;
            try {
                auto fut = pool.enqueue([idx, &world]() -> gldx::VoxelChunkMesh {
                    std::shared_lock<std::shared_mutex> lk(world.mx);
                    const gldx::Chunk& ch = *world.cpu[idx];
                    // Private 8 KB snapshot of the cells, taken under the lock:
                    // the mesher never sees a torn view of a concurrent edit.
                    const std::array<std::uint16_t,
                                     static_cast<std::size_t>(gldx::kChunkSize)
                                         * gldx::kChunkSize * gldx::kChunkSize> cells = ch.blocks();
                    return world.mesher.Build(cells.data(), ch.origin(), world);
                });
                meshPending.push_back({idx, std::move(fut)});
            } catch (const std::future_error&) {
                world.state[idx].phase = kGenerated;   // pool gone; retry later
            }
        };

        auto submitGen = [&](int idx, glm::ivec3 g) {
            world.state[idx].phase = kGenerating;
            try {
                auto fut = pool.enqueue([g, &world]() -> GenResult {
                    auto ch = std::make_unique<gldx::Chunk>(g * kChunk);
                    const bool any = GenerateChunk(*ch, world);
                    return {std::move(ch), any};
                });
                genPending.push_back({idx, std::move(fut)});
            } catch (const std::future_error&) {
                world.state[idx].phase = kEmpty;
            }
        };

        auto streamChunks = [&](const glm::vec3& eye) {
            const int ccx = std::clamp(static_cast<int>(eye.x) / kChunk, 0, kGridX - 1);
            const int ccy = std::clamp(static_cast<int>(eye.y) / kChunk, 0, kGridY - 1);
            const int ccz = std::clamp(static_cast<int>(eye.z) / kChunk, 0, kGridZ - 1);
            // Publish the ring centre before anything asks who is ready.
            world.camChunk = {ccx, ccz};

            candidates.clear();
            for (int dz = -kRenderDistance; dz <= kRenderDistance; ++dz) {
                for (int dx = -kRenderDistance; dx <= kRenderDistance; ++dx) {
                    const int rr = dx * dx + dz * dz;
                    if (rr > kRenderDistance * kRenderDistance) continue;
                    const int cz = ccz + dz, cx = ccx + dx;
                    if (cx < 0 || cz < 0 || cx >= kGridX || cz >= kGridZ) continue;
                    for (int cy = 0; cy < kGridY; ++cy) {
                        const int idx = World::Index(cx, cy, cz);
                        std::uint8_t& phase = world.state[idx].phase;
                        // The height test costs a few noise lookups; running it
                        // once and remembering the answer as kVoid keeps the
                        // per-frame cost at a comparison.
                        if (phase == kEmpty && !world.ColumnRelevant(cx, cy, cz))
                            phase = kVoid;
                        if (phase == kVoid) continue;
                        candidates.emplace_back(rr + std::abs(cy - ccy),
                                                glm::ivec3{cx, cy, cz});
                    }
                }
            }
            std::sort(candidates.begin(), candidates.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            // Nearest-first: request generation, then mesh what just became
            // complete (its 3x3x3 neighbourhood must exist first), then remesh
            // whatever an edit dirtied.
            for (const auto& cand : candidates) {
                const glm::ivec3 g = cand.second;
                const int idx = World::Index(g.x, g.y, g.z);
                const std::uint8_t phase = world.state[idx].phase;
                if (phase == kEmpty) {
                    if (genPending.size() < kMaxGenInFlight) submitGen(idx, g);
                } else if (phase == kGenerated) {
                    if (meshPending.size() >= kMaxMeshInFlight) continue;
                    std::shared_lock<std::shared_mutex> lk(world.mx);
                    if (world.NeighborReady(g.x, g.y, g.z)) {
                        lk.unlock();
                        submitMesh(idx);
                    }
                } else if (phase == kReady) {
                    std::shared_lock<std::shared_mutex> lk(world.mx);
                    const gldx::Chunk* ch = world.cpu[idx].get();
                    const bool dirty = ch && ch->dirty();
                    lk.unlock();
                    if (dirty && meshPending.size() < kMaxMeshInFlight) submitMesh(idx);
                }
            }

            // Drain finished generation: install the chunk and wake up any
            // Ready neighbour whose border now disagrees with it.
            for (auto it = genPending.begin(); it != genPending.end();) {
                if (it->fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    ++it;
                    continue;
                }
                const int idx = it->idx;
                GenResult result = it->fut.get();
                std::unique_ptr<gldx::Chunk> ch = std::move(result.first);
                const glm::ivec3 g = ch->origin() / kChunk;
                if (!result.second) {
                    // Pure air after all: cache the verdict instead of the
                    // data. Neighbours stop waiting on it (NeighborReady) and
                    // the streaming loop stops proposing it.
                    world.state[idx].phase = kVoid;
                    it = genPending.erase(it);
                    continue;
                }
                {
                    std::unique_lock<std::shared_mutex> lk(world.mx);
                    world.cpu[idx] = std::move(ch);
                    for (const glm::ivec3& off : kSide) {
                        const glm::ivec3 nb = g + off;
                        if (!World::InRange(nb.x, nb.y, nb.z)) continue;
                        const int nidx = World::Index(nb.x, nb.y, nb.z);
                        if (world.state[nidx].phase == kReady && world.cpu[nidx])
                            world.cpu[nidx]->MarkDirty();
                    }
                }
                world.state[idx].phase = kGenerated;
                ++genCount;
                it = genPending.erase(it);
            }

            // Drain finished meshing and upload (render thread only, that is
            // why the queue is drained here rather than in the worker). Buffer
            // swaps are the expensive part, so a frame takes a fixed few.
            std::size_t uploads = 0;
            for (auto it = meshPending.begin(); it != meshPending.end();) {
                if (uploads >= kMaxUploadsPerFrame) break;
                if (it->fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    ++it;
                    continue;
                }
                const int idx = it->idx;
                gldx::VoxelChunkMesh mesh = it->fut.get();
                ++uploads;
                gldx::Chunk& ch = *world.cpu[idx];
                gldx::VoxelChunkGpu& rec = world.gpu[idx];
                const glm::ivec3 g = ch.origin() / kChunk;

                // A live opaque VAO is also the record's "built" flag: the
                // first build is where the transform and bounds get filled.
                if (rec.opaque.valid()) {
                    rec.opaque.Update(std::move(mesh.opaque));
                } else {
                    rec.origin = g * kChunk;
                    rec.model = glm::translate(glm::mat4(1.0f), glm::vec3(rec.origin));
                    rec.center = glm::vec3(rec.origin) + glm::vec3(kChunk * 0.5f);
                    rec.radius = gldx::Chunk::circumRadius();
                    rec.opaque.Upload(std::move(mesh.opaque));
                }
                // Water is optional per chunk. An empty mesh leaves the buffer
                // alone instead of asking GL for a zero-size upload; the
                // indexCount() check below is what actually gates the pass.
                if (!mesh.transparent.indices.empty()) {
                    if (rec.transparent.valid()) rec.transparent.Update(std::move(mesh.transparent));
                    else rec.transparent.Upload(std::move(mesh.transparent));
                }
                rec.hasTransparent = rec.transparent.indexCount() > 0;

                {
                    std::unique_lock<std::shared_mutex> lk(world.mx);
                    for (int s = 0; s < 6; ++s) {
                        if (!ch.neighborDirty(s)) continue;
                        const glm::ivec3 nb = g + kSide[s];
                        if (!World::InRange(nb.x, nb.y, nb.z)) continue;
                        const int nidx = World::Index(nb.x, nb.y, nb.z);
                        if (world.state[nidx].phase == kReady && world.cpu[nidx])
                            world.cpu[nidx]->MarkDirty();
                    }
                    ch.ClearNeighborDirty();
                }
                world.state[idx].phase = kReady;
                ++meshCount;
                it = meshPending.erase(it);
            }
        };

        const bool scripted = flags.number("freeze-at") > 0.0;
        ApplyCapture(window, input, scripted);
        const double startedAt = glfwGetTime();
        const double quitAfter = flags.quitAfter();
        const double freezeAt = flags.number("freeze-at");
        // Everything time-driven (flight speed, water scroll, debris) advances on
        // this clock rather than on wall time, so --freeze-at can park it.
        double animTime = startedAt;
        double lastAnimTime = animTime;
        // Debris integrates on a fixed 120 Hz step counted off that clock instead
        // of the frame delta. Two reasons, one of them a test: with a variable
        // step, the parked burst sat at a different height in every run, which put
        // the noise floor of a particles-off screenshot diff at 2% of the frame
        // and made the switch unmeasurable; in play it means the same puff falls
        // identically at 30 and at 300 fps.
        constexpr double kSimStep = 1.0 / 120.0;
        int simSteps = 0;
        double smoothedFps = 60.0;
        // Fixed-window frame counter for the HUD readout (see the loop).
        int fpsFrames = 0;
        double fpsWindowStart = startedAt;
        static const char* const kBlockKeys[9] = {
            "", "1 grass", "2 dirt", "3 stone", "4 sand",
            "5 wood", "6 leaves", "7 water", "8 snow"
        };
        // The HUD shows the previous frame's cull counters: the passes write
        // them into the frame, which is assembled after the text is built.
        int prevVisible = 0, prevTotal = 0;
        // --auto-break N / --auto-place N: scripted mining and building, for the
        // headless feature sweep. Each tick goes through exactly the same path a
        // click does (DDA pick -> Edit -> remesh -> debris), so a screenshot taken
        // after it says something about all four without a keyboard attached.
        // Scripted edits tick every 0.25 s of fixed-step time, so their cadence is
        // as reproducible as the debris they spawn. The upper bound is not
        // cosmetic: the two counters are summed below, so saturating straight at
        // INT_MAX would overflow that addition.
        constexpr int kMaxScriptedEdits = 100000;
        int autoBreakLeft = std::clamp(flags.integer("auto-break"), 0, kMaxScriptedEdits);
        int autoPlaceLeft = std::clamp(flags.integer("auto-place"), 0, kMaxScriptedEdits);
        constexpr int kBreakPeriodSteps = 30;
        int nextBreakStep = 240;                              // first tick 2 s in
        int spawnedTotal = 0, frames = 0, breaks = 0;
        double minFps = 1e9;

        while (!glfwWindowShouldClose(window)) {
            // ---- edge-detected toggles + keys -------------------------------
            auto edge = [](bool down, bool& armed) {
                if (down && armed) { armed = false; return true; }
                if (!down) armed = true;
                return false;
            };
            static bool flyArmed = true, escArmed = true, partArmed = true, orthoArmed = true;
            static bool bsArmed = true;
            if (edge(glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS, flyArmed)) {
                input.cam = (input.cam == Input::Cam::Fly) ? Input::Cam::Orbit
                                                            : Input::Cam::Fly;
                if (input.cam == Input::Cam::Orbit) input.focus = camera.Position();
                ApplyCapture(window, input, scripted);
            }
            if (edge(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS, escArmed)) {
                if (input.cam == Input::Cam::Fly) {
                    input.captured = !input.captured;
                    glfwSetInputMode(window, GLFW_CURSOR, input.captured
                                                          ? GLFW_CURSOR_DISABLED
                                                          : GLFW_CURSOR_NORMAL);
                } else {
                    glfwSetWindowShouldClose(window, true);
                }
            }
            if (edge(glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS, partArmed))
                input.showParticles = !input.showParticles;
            if (glfwGetKey(window, GLFW_KEY_X) == GLFW_PRESS)
                glfwSetWindowShouldClose(window, true);
            for (int k = 0; k < 8; ++k) {
                if (glfwGetKey(window, GLFW_KEY_1 + k) == GLFW_PRESS)
                    input.selected = k + 1;
            }
            if (edge(glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS, orthoArmed)) {
                // The camera owns the projection state; the demo only mirrors
                // the result here for the HUD line.
                input.ortho = camera.ToggleProjection()
                              == gldx::Camera::Projection::Orthographic;
            }
            if (edge(glfwGetKey(window, GLFW_KEY_B) == GLFW_PRESS, bsArmed))
                input.doubleSided = !input.doubleSided;

            const double now = glfwGetTime();
            // --freeze-at S parks the demo clock S seconds after start: dt falls to
            // 0, so the scroll, the debris and the scripted breaks all stop, and
            // two runs with the same settings become pixel-identical for a sweep.
            // The clamp is applied to *elapsed* seconds rather than to an absolute
            // timestamp: (startedAt + S) - startedAt is not exactly S in binary
            // floating point, and the lost ulp was enough to make the fixed-step
            // count below flicker between 959 and 960 - one step of debris, a few
            // pixels of pure noise in every screenshot pair.
            const double elapsed = freezeAt > 0.0 ? std::min(now - startedAt, freezeAt)
                                                 : now - startedAt;
            animTime = startedAt + elapsed;
            const float dt = static_cast<float>(std::min(animTime - lastAnimTime, 0.25));
            lastAnimTime = animTime;
            // Catch the fixed-step clock up with the demo clock. Both are clamped
            // by --freeze-at, so the step count reached at the parked moment is a
            // function of S alone and no frame pacing can shift it. The one thing
            // pacing can still shift is *where a frame starts*: a slow frame
            // advances two or three steps at once and can jump clean over a
            // scripted-edit boundary, leaving that burst a step older here than
            // there. So the boundary is used as an intermediate target - the loop
            // stops on it, the edit below runs, and the rest of the frame's steps
            // are taken afterwards.
            const int targetSteps = static_cast<int>(elapsed / kSimStep + 0.5);
            int stepGoal = targetSteps;
            if (autoBreakLeft + autoPlaceLeft > 0)
                stepGoal = std::min(targetSteps, nextBreakStep);
            while (simSteps < stepGoal) {
                ++simSteps;
                particles.Update(static_cast<float>(kSimStep));
            }
            // Averaged over a fixed wall-clock window instead of an EMA of 1/dt
            // per frame: the latter is dominated by whichever frame last
            // stalled, so the number never agreed with the frame it was drawn on.
            ++fpsFrames;
            if (const double span = now - fpsWindowStart; span >= 0.5) {
                smoothedFps = static_cast<float>(fpsFrames) / static_cast<float>(span);
                minFps = std::min(minFps, static_cast<double>(smoothedFps));
                fpsFrames = 0;
                fpsWindowStart = now;
            }
            if (quitAfter > 0.0 && now - startedAt >= quitAfter)
                glfwSetWindowShouldClose(window, true);
            ++frames;

            // ---- sun direction ([ ] azimuth, - = elevation) ------------------
            // Polled rather than edge-triggered: re-aiming the sun is a dragging
            // sort of job, and scaling by dt keeps the sweep rate the same on a
            // 30 fps laptop as on a 300 fps one.
            constexpr float kSunRate = 0.6f;
            if (glfwGetKey(window, GLFW_KEY_LEFT_BRACKET) == GLFW_PRESS)
                input.sunAzimuth -= kSunRate * dt;
            if (glfwGetKey(window, GLFW_KEY_RIGHT_BRACKET) == GLFW_PRESS)
                input.sunAzimuth += kSunRate * dt;
            if (glfwGetKey(window, GLFW_KEY_MINUS) == GLFW_PRESS)
                input.sunElevation = std::max(0.05f, input.sunElevation - kSunRate * dt);
            if (glfwGetKey(window, GLFW_KEY_EQUAL) == GLFW_PRESS)
                input.sunElevation = std::min(1.45f, input.sunElevation + kSunRate * dt);

            int fbw = 0, fbh = 0;
            glfwGetFramebufferSize(window, &fbw, &fbh);
            camera.SetViewportAspect(fbh > 0 ? static_cast<float>(fbw) / fbh : 1.0f);
            camera.SetOrbitRadius(input.orbitRadius);

            // ---- movement ---------------------------------------------------
            const bool boost = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
            const float step = input.speed * (boost ? 0.35f : 1.0f) * dt;
            if (input.cam == Input::Cam::Fly) {
                camera.SetYawPitch(input.yaw, input.pitch);
                // Build the intended displacement from the camera basis instead
                // of moving the eye directly, so it can be resolved against the
                // world: the same Forward/Right/Up axes MoveForward et al use.
                glm::vec3 delta(0.0f);
                if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) delta += camera.Forward();
                if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) delta -= camera.Forward();
                if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) delta -= camera.Right();
                if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) delta += camera.Right();
                if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) delta += camera.Up();
                if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) delta -= camera.Up();
                delta *= step;

                // The eye sits kEyeHeight above the feet; the collider moves the
                // feet, so terrain under the player is what stops the descent (a
                // real floor) rather than the old sea-level teleport that made
                // the camera refuse to go down below the water line.
                const gldx::VoxelBody playerBody{0.3f, 1.9f};
                glm::vec3 feet = camera.Position();
                feet.y -= kEyeHeight;
                // Sub-step so one resolve never advances the body past half a
                // cell: a stalled frame's dt clamp would otherwise let a full
                // step several cells long tunnel thin walls (see Collision.h).
                const float len = glm::length(delta);
                const int segs = len > 0.5f ? static_cast<int>(std::ceil(len / 0.5f)) : 1;
                const glm::vec3 seg = delta / static_cast<float>(segs);
                {
                    std::shared_lock<std::shared_mutex> lk(world.mx);
                    for (int s = 0; s < segs; ++s)
                        gldx::MoveVoxelAabb(feet, playerBody, seg,
                                           [&world](glm::ivec3 c) { return world.Pickable(c); });
                }
                feet.y += kEyeHeight;
                // Keep the eye inside the world box even where chunks are not
                // streamed in yet (there the collider sees air and cannot stop).
                feet.x = std::clamp(feet.x, 1.0f, static_cast<float>(kWorldX) - 1.0f);
                feet.z = std::clamp(feet.z, 1.0f, static_cast<float>(kWorldZ) - 1.0f);
                feet.y = std::clamp(feet.y, 1.0f + kEyeHeight, static_cast<float>(kWorldY) - 2.0f);
                camera.Translate(feet - camera.Position());
            } else {
                const float cp = std::cos(input.orbitPitch);
                const glm::vec3 eye(
                    input.focus.x + input.orbitRadius * cp * std::sin(-input.orbitYaw),
                    input.focus.y + input.orbitRadius * std::sin(input.orbitPitch),
                    input.focus.z + input.orbitRadius * cp * std::cos(-input.orbitYaw));
                camera.LookAt(eye, input.focus, glm::vec3(0, 1, 0));
            }

            // ---- block editing (DDA pick through the screen centre) ---------
            bool scriptedBreak = false;
            if (autoBreakLeft + autoPlaceLeft > 0 && simSteps >= nextBreakStep) {
                // Re-anchor on the *absolute* cadence (period multiples from the
                // first tick), not on "now + period": the latter let the frame
                // that happened to cross the 2 s mark shift every later tick by
                // however many steps it had jumped, so the last scripted break
                // fell inside or outside --freeze-at depending on frame pacing.
                // Ticks missed during a stall are dropped, never replayed.
                nextBreakStep += kBreakPeriodSteps;
                if (nextBreakStep <= simSteps) nextBreakStep = simSteps + kBreakPeriodSteps;
                if (autoBreakLeft > 0) {
                    --autoBreakLeft;
                    input.wantBreak = true;
                    scriptedBreak = true;
                } else {
                    --autoPlaceLeft;
                    input.wantPlace = true;
                }
            }
            if (input.wantBreak || input.wantPlace) {
                const gldx::Ray ray = gldx::PickRay(fbw * 0.5f, fbh * 0.5f, fbw, fbh,
                                                  camera.InverseViewProjection());
                std::optional<gldx::VoxelHit> hit;
                {
                    std::shared_lock<std::shared_mutex> lk(world.mx);
                    hit = gldx::RaycastVoxel(
                        ray, glm::ivec3{0, 0, 0},
                        glm::ivec3{kWorldX - 1, kWorldY - 1, kWorldZ - 1},
                        [&world](glm::ivec3 c) { return world.Pickable(c); }, kReach);
                }
                if (hit) {
                    const BlockId broken = [&]() {
                        std::shared_lock<std::shared_mutex> lk(world.mx);
                        return world.Sample(hit->position);
                    }();
                    if (input.wantBreak) {
                        ++breaks;   // reported at exit: proves the scripted sweep mined
                        world.Edit(hit->position, 0);
                        water.schedule(hit->position);   // water above may pour in
                        // Debris: a short outward burst, tinted by the block's
                        // own slice colour so the puff reads as "that material".
                        const glm::vec3 tint = BlockTint(broken);
                        const glm::vec3 c = glm::vec3(hit->position) + 0.5f;
                        for (int i = 0; i < 22 && input.showParticles; ++i) {
                            // Seeded off the break counter, not the clock: an
                            // animTime-derived seed changed wholesale whenever the
                            // frame happened to land on a different tenth of a
                            // second, which made each puff irreproducible.
                            const std::uint32_t h = Hash2(i * 7, breaks * 31 + i);
                            const glm::vec3 v(
                                (static_cast<float>(h & 255) / 255.0f - 0.5f) * 4.0f,
                                1.2f + static_cast<float>((h >> 8) & 255) / 255.0f * 2.4f,
                                (static_cast<float>((h >> 16) & 255) / 255.0f - 0.5f) * 4.0f);
                            if (particles.Spawn(c + 0.4f * glm::vec3(hit->normal), v,
                                                glm::vec4(tint, 1.0f), 0.55f, 0.14f))
                                ++spawnedTotal;
                        }
                        // Drop the eye with the floor just removed: a shaft dug
                        // under the feet otherwise gets deeper than the
                        // interaction reach and the remaining ticks would pick
                        // empty air.
                        if (scriptedBreak) camera.Translate({0.0f, -1.0f, 0.0f});
                    }
                    if (input.wantPlace) {
                        const glm::ivec3 target = hit->position + hit->normal;
                        // Never seal the player inside a block.
                        const glm::ivec3 eyeCell{static_cast<int>(camera.Position().x),
                                                 static_cast<int>(camera.Position().y),
                                                 static_cast<int>(camera.Position().z)};
                        if (std::abs(target.x - eyeCell.x) + std::abs(target.y - eyeCell.y)
                                + std::abs(target.z - eyeCell.z) > 1) {
                            world.Edit(target, static_cast<BlockId>(input.selected));
                            water.schedule(target);
                        }
                    }
                }
                input.wantBreak = input.wantPlace = false;
            }
            // The steps this frame skipped on the way to the boundary.
            while (simSteps < targetSteps) {
                ++simSteps;
                particles.Update(static_cast<float>(kSimStep));
            }

            streamChunks(camera.Position());
            water.step(world, dt);

            // ---- lighting + fog ---------------------------------------------
            const glm::vec3 towardSun = SunToward(input);
            gldx::LightSetup setup;
            setup.sun.direction = -towardSun;
            setup.sun.color = glm::vec3(1.0f, 0.96f, 0.88f);
            setup.sun.intensity = 2.2f;
            setup.ambient = glm::vec3(0.30f, 0.34f, 0.40f);
            // Fog closes just inside the render distance, so chunk pop-in
            // happens behind the haze rather than in front of the camera.
            // Leaving fogStart at 0 is what switches it off in the shader.
            setup.fogColor = ToLinear(glm::vec3(0.60f, 0.72f, 0.88f));
            if (input.showFog) {
                setup.fogStart = static_cast<float>(kRenderDistance) * kChunk * 0.35f;
                setup.fogEnd   = static_cast<float>(kRenderDistance) * kChunk * 0.98f;
            }
            lightBuffer.Update(setup, camera.Position());

            const glm::mat4 viewProj = camera.ViewProjection();
            gldx::Frustum frustum;
            frustum.Extract(viewProj);

            vx.time = static_cast<float>(elapsed);
            vx.doubleSided = input.doubleSided;

            char line[512];
            std::snprintf(line, sizeof(line),
                          "voxel_demo %s  %s\n"
                          "%.0f fps   CPU %.2f ms  GPU %.2f ms   chunks %d (meshed %d)  pos %.0f %.0f %.0f\n"
                          "streaming gen %zu mesh %zu   particles %zu   visible %d/%d\n"
                          "picked: %s   (WASD fly, space/ctrl up/down, shift slow, LMB break, RMB place)\n"
                          "F camera %s   ESC pointer %s   P particles %s   B 2-sided %s   [ ] - = sun   X quit",
                          gldx::kAppVersion,
                          input.cam == Input::Cam::Fly ? "FLY" : "ORBIT",
                          smoothedFps,
                          profiler.CpuMs(), profiler.GpuMs(),
                          genCount, meshCount,
                          camera.Position().x, camera.Position().y, camera.Position().z,
                          genPending.size(), meshPending.size(), particles.count(),
                          prevVisible, prevTotal,
                          kBlockKeys[std::min(input.selected, 8)],
                          input.cam == Input::Cam::Fly ? "-> orbit" : "-> fly",
                          input.captured ? "captured" : "free",
                          input.showParticles ? "on" : "off",
                          input.doubleSided ? "on" : "off");
            hudRaw->text = line;
            hudRaw->crosshair = (input.cam == Input::Cam::Fly);

            gldx::RenderFrame frame;
            frame.camera = &camera;
            frame.frustum = &frustum;
            frame.viewProj = viewProj;
            frame.post = &post;
            frame.lights = &lightBuffer;
            frame.lightSetup = setup;
            frame.shadow.sunToward = towardSun;
            frame.sky.env = &env;
            frame.sky.box = input.showSky ? &skybox : nullptr;
            frame.overlay.sprite = &sprite;
            frame.overlay.font = &font;
            frame.overlay.white = &white;
            frame.overlay.profiler = &profiler;
            frame.particles = input.showParticles ? &particles : nullptr;
            frame.useBloom = false;
            frame.fbWidth = fbw;
            frame.fbHeight = fbh;
            frame.smoothedFps = smoothedFps;
            frame.ortho = input.ortho;

            profiler.BeginFrame();
            renderer.Render(frame);
            prevVisible = frame.visibleCount;
            prevTotal   = frame.totalNodes;

            glfwSwapBuffers(window);
            glfwPollEvents();
        }

        // One summary line per run: the counters a scripted sweep or a CI smoke
        // cannot read back out of a screenshot (streaming convergence in
        // particular - the HUD shows them, but only to whoever is looking).
        const double ranFor = glfwGetTime() - startedAt;
        std::printf("voxel_demo: ran %.1fs  %d frames  fps avg %.1f min %.1f   "
                    "chunks gen %d meshed %d   blocks broken %d   particles live %zu spawned %d\n",
                    ranFor, frames,
                    ranFor > 0.0 ? static_cast<double>(frames) / ranFor : 0.0,
                    minFps > 1e8 ? 0.0 : minFps,
                    genCount, meshCount, breaks, particles.count(), spawnedTotal);

        pool.shutdown();
        return 0;   // GPU owners destruct here, on the render thread
    };

    const int rc = runDemo();
    glfwDestroyWindow(window);
    glfwTerminate();
    return rc;
}
