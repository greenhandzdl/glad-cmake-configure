#ifndef GFX_VOXEL_BLOCKREGISTRY_H
#define GFX_VOXEL_BLOCKREGISTRY_H

/**
 * @file BlockRegistry.h
 * @brief Block-type table: id -> rendering/behavioural attributes (pure CPU).
 *
 * A voxel world stores small integer block ids per cell; everything the
 * mesher, the shaders and the demo gameplay need (which texture-array slice
 * to sample, whether the cell occludes its neighbours, whether its faces go
 * to the transparent pass, whether it emits light) is described once here.
 *
 * The default-constructed registry carries the engine's built-in block set
 * (ids below). Callers may Append() extra types; ids are stable indices.
 * No GL, so a registry can be built/shared on any thread; treat it as
 * read-only once world generation has started.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace gfx {

struct BlockDef {
    std::string name = "air";
    int  texLayer = 0;          // slice index into the block Texture2DArray
    bool solid = false;         // occludes neighbour faces + blocks the DDA ray
    bool cutout = false;        // alpha-tested foliage: opaque pass w/ discard
    bool transparent = false;   // blended pass (water/ice), still occludes
    bool emissive = false;      // reserved: feeds the future light propagation
};

class BlockRegistry {
public:
    // Built-in ids (0 must stay air: chunks zero-initialise to empty).
    enum Id : std::uint16_t {
        kAir = 0,
        kGrass,
        kDirt,
        kStone,
        kSand,
        kWood,
        kLeaves,
        kWater,
        kSnow,
        kBuiltinCount,
    };

    BlockRegistry() : defs_{
        // texLayer values match the procedural atlas the demos generate. The
        // built-in table is positional, so it is written straight into defs_:
        // index == id, no Add() calls whose return value would go unused.
        {"air",     0, false, false, false, false},   // 0
        {"grass",   1, true,  false, false, false},   // 1
        {"dirt",    2, true,  false, false, false},   // 2
        {"stone",   3, true,  false, false, false},   // 3
        {"sand",    4, true,  false, false, false},   // 4
        {"wood",    5, true,  false, false, false},   // 5
        {"leaves",  6, true,  true,  false, false},   // 6 alpha-tested
        {"water",   7, true,  false, true,  false},   // 7 blended pass
        {"snow",    8, true,  false, false, false},   // 8
    } {}

    // Runtime extension point: appended ids continue from the built-in range.
    // Nodiscard because a caller that appends must know the id it just created.
    [[nodiscard]] std::uint16_t Add(BlockDef def) {
        defs_.push_back(std::move(def));
        return static_cast<std::uint16_t>(defs_.size() - 1);
    }

    [[nodiscard]] std::uint16_t count() const noexcept {
        return static_cast<std::uint16_t>(defs_.size());
    }

    // Out-of-range ids read as air, so a chunk never crashes on a bad id.
    [[nodiscard]] const BlockDef& Get(std::uint16_t id) const {
        static const BlockDef kAirDef{"air", 0, false, false, false, false};
        return id < defs_.size() ? defs_[id] : kAirDef;
    }

    // Meshing conveniences.
    [[nodiscard]] bool IsOpaque(std::uint16_t id) const {
        const BlockDef& d = Get(id);
        return d.solid && !d.transparent && !d.cutout;
    }
    [[nodiscard]] bool IsTransparent(std::uint16_t id) const {
        return Get(id).transparent;
    }

private:
    std::vector<BlockDef> defs_;
};

} // namespace gfx

#endif // GFX_VOXEL_BLOCKREGISTRY_H
