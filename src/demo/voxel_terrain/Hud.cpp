// voxel_terrain demo — HUD render pass implementation (see Hud.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Hud.h"

#include <glm/glm.hpp>

namespace voxel_terrain {

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

} // namespace voxel_terrain
