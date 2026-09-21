#ifndef VOXEL_TERRAIN_HUD_H
#define VOXEL_TERRAIN_HUD_H

/**
 * @file Hud.h
 * @brief The voxel_terrain demo's HUD render pass.
 *
 * Reuses the engine's sprite + text renderers, then closes the frame in the
 * profiler. Demo-owned on purpose - the library's DebugHudPass prints the PBR
 * demo's scene-graph counters, which say nothing here.
 *
 * NOTE: names gldx engine types, so include AFTER `import gldx;`.
 */

#include <string>

namespace voxel_terrain {

class VoxelHudPass final : public gldx::RenderPass {
public:
    VoxelHudPass() : gldx::RenderPass("VoxelHud") {}

    void Execute(gldx::RenderFrame& f) override;

    std::string text;
    bool crosshair = false;
};

} // namespace voxel_terrain

#endif // VOXEL_TERRAIN_HUD_H
