#ifndef GLASS_WATER_HUD_H
#define GLASS_WATER_HUD_H

/**
 * @file Hud.h
 * @brief The glass_water HUD: the four switches, the GL state as the driver holds
 *        it, and the order the blend layer really went out in.
 *
 * The overlay is the demo's instrument panel, so it prints evidence rather than
 * settings. Two lines in particular are worth reading literally: `depth write`
 * comes from glGetIntegerv(GL_DEPTH_WRITEMASK) inside the pass rather than from
 * the flag that asked for it, and `order` is the sequence the previous frame was
 * actually submitted in - so turning sorting off visibly rewrites the list
 * instead of only changing a label.
 *
 * NOTE: names gldx engine types, so include AFTER `import gldx;`.
 */

#include <string>

#include "BlendPass.h"
#include "Input.h"

namespace glass_water {

class GlassHudPass final : public gldx::RenderPass {
public:
    GlassHudPass() : gldx::RenderPass("GlassHud") {}

    void Execute(gldx::RenderFrame& f) override;

    // Non-owning: the pass whose output is being reported, and the state being
    // toggled. Both outlive the HUD, which is last in the pipeline.
    const BlendPass* blend = nullptr;
    const Input* in = nullptr;
};

} // namespace glass_water

#endif // GLASS_WATER_HUD_H
