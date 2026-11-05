#ifndef NIGHT_FOREST_HUD_H
#define NIGHT_FOREST_HUD_H

/**
 * @file Hud.h
 * @brief The night_forest instrument panel: what the instancing cost, what the
 *        crate's cube was made of, and where the wandering crate got to.
 *
 * The overlay prints evidence rather than settings wherever the two can disagree.
 * `env` comes from the texture unit the pass actually bound, so switching capture
 * off rewrites the line rather than a label; `faces` is how many views were
 * rendered into the cube on the last frame, which is why a parked crate reports
 * faces 0 while still showing a live capture; `world` is the draw and instance
 * counts the pass tallied as it submitted, which is the demo's whole claim in two
 * numbers - fourteen hundred trees arriving as two draws.
 *
 * Like every other demo here, the panel is switchable off with H and with
 * `--off hud`, and the comparison screenshots must use that: a live fps number
 * puts a text diff into every pair of images.
 *
 * NOTE: names gldx engine types through Passes.h, so include AFTER `import gldx;`.
 */

#include <string>

#include "Input.h"
#include "Sky.h"
#include "Passes.h"

namespace night_forest {

class NightHudPass final : public gldx::RenderPass {
public:
    NightHudPass() : gldx::RenderPass("NightHud") {}

    void Execute(gldx::RenderFrame& f) override;

    // Non-owning: everything here outlives the HUD, which is last in the pipeline.
    const NightPass* pass = nullptr;
    const Scene*     scene = nullptr;
    const Input*     in = nullptr;
    const StarField* field = nullptr;

    // What the sky actually is, as a sentence main.cpp can write once: the
    // procedural field or the panorama that was reprojected, with its size and
    // seed. Printed because "the sky is a cube map" is true in both cases and only
    // this line says which one produced the frame.
    std::string skySource;

    // Framing the pass owns but the panel reports: the Gooch band and the crate's
    // optical constants, so a tweaked uniform that never reached the GPU is visible
    // as the line failing to change.
    float ior = 1.45f;
    float envLod = 1.5f;
    float bandWidth = 0.16f;
    float fogStart = 45.0f;
    float fogEnd = 190.0f;

    // The clock the frame was simulated at, and the freeze that parked it.
    double animTime = 0.0;
    double frozenAt = 0.0;
};

} // namespace night_forest

#endif // NIGHT_FOREST_HUD_H
