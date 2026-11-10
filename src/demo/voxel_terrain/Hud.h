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
 * The panel is built from a string the simulation writes each frame, which is
 * the engine Hud's intended intake: main.cpp owns what the lines say, Hud.cpp
 * only decides which section they go in, and gldx::Hud owns the palette, the
 * sizing (the 560 x 116 box this file used to hard-code is gone) and the batch.
 * The controls block moved out of that string into its own bottom-left panel,
 * so the streaming counters stay readable while the keys are always on screen.
 *
 * NOTE: names gldx engine types, so include AFTER `import gldx;`.
 */

#include <string>

namespace voxel_terrain {

class VoxelHudPass final : public gldx::HudPass {
public:
    VoxelHudPass() : gldx::HudPass("VoxelHud") {}

    // Refills the inherited `hud` from `text` and `crosshair`.
    void Build(gldx::RenderFrame& f) override;

    // The evidence lines, newline-joined, written by the frame callback before
    // the passes run. Build() splits them into lines; nothing here formats them.
    std::string text;
    bool crosshair = false;
};

} // namespace voxel_terrain

#endif // VOXEL_TERRAIN_HUD_H
