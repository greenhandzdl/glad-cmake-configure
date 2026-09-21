// voxel_terrain demo — world-layer implementation (see World.h).
#include "gldx/core/Platform.h"

import gldx;

#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include <glm/glm.hpp>

namespace voxel_terrain {

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

BlockId SurfaceBlock(int topY) {
    if (topY <= kSeaLevel + 1) return Block::kSand;
    if (topY > 78) return Block::kSnow;
    return Block::kGrass;
}

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

glm::vec3 ToLinear(const glm::vec3& srgb) {
    return {static_cast<float>(std::pow(srgb.r, 2.2)),
            static_cast<float>(std::pow(srgb.g, 2.2)),
            static_cast<float>(std::pow(srgb.b, 2.2))};
}

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

void WaterSim::step(World& world, float dt) {
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

} // namespace voxel_terrain
