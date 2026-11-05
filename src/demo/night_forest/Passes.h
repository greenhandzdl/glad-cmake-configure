#ifndef NIGHT_FOREST_PASSES_H
#define NIGHT_FOREST_PASSES_H

/**
 * @file Passes.h
 * @brief The night_forest frame: one program, two render targets, one pass.
 *
 * The pass has two jobs and does them in this order:
 *
 *   1. capture - render the world six times into a cube map attached to an FBO,
 *      once per face, from the crate's centre. That cube is what the crate then
 *      reflects and refracts, which is why the capture has to happen first: the
 *      crate is the only object that is *not* allowed in the capture, and
 *      drawing it there would make it reflect itself and settle into whatever
 *      orientation it happened to have last frame.
 *   2. draw - open the HDR scene target and draw the same world once, for the
 *      window, with the crate in it.
 *
 * Two jobs in one pass rather than two passes is a deliberate call, not a
 * shortcut: the capture has no meaning without the program, the uniforms and the
 * draw list of the frame it feeds, and splitting them would mean passing the whole
 * world context across a boundary that adds nothing. `--off capture` therefore
 * does not remove a stage - it removes the *content* of unit 11, and the crate
 * falls back to sampling the static star cube. That is the comparison the demo
 * wants you to make: a mirrored box showing the sky, versus one showing the
 * forest, the lanterns and the ground behind you.
 *
 * Face views come from the measured cube-face table in Sky.h, which is the same
 * table the procedural star field is painted with. That identity is the point:
 * paint a moon at one direction, capture along six faces with those directions,
 * and the moon must reappear in the reflection of the face it belongs to. If the
 * two tables ever disagreed, the sky would tile seamlessly and still be wrong.
 *
 * NOTE: names gldx engine types, so include AFTER `import gldx;`.
 */

#include <cstdint>

#include <glm/glm.hpp>

#include "Scene.h"

namespace night_forest {

class NightPass final : public gldx::RenderPass {
public:
    NightPass() : gldx::RenderPass("NightForest") {}

    // Compiles the one program the whole world is drawn with. Render-thread only.
    bool Init();

    struct Settings {
        const Scene* scene = nullptr;
        // The star cube: the background, and the environment the crate samples
        // when there is no capture. Non-owning; owned by main.cpp.
        const gldx::TextureCubeMap* sky = nullptr;
        gldx::SkyboxRenderer* skybox = nullptr;

        // ---- Gooch band ----
        float bandWidth = 0.16f;         // terminator half-width, remapped units
        float specPower = 42.0f;
        float specStrength = 0.10f;      // added, not multiplied; ~10 % reads right
        glm::vec3 lightTint {1.30f, 1.06f, 0.76f};   // the warm half of the pair
        // The cold half. Multiplied into the albedo rather than replacing it, so a
        // pine's shade is a dark blue-green and a birch's is a dark blue - one
        // shadow colour for a whole scene is the thing that makes a sketch look
        // like a paint-by-numbers.
        glm::vec3 shadowTint{0.34f, 0.44f, 0.80f};
        // The floor under the shade side, outside the moon's scale: the reason a
        // 0.30-intensity moonlight is a night scene rather than a black one, and
        // small enough that it reads as a colour and not as a second light.
        glm::vec3 shadowLift{0.012f, 0.018f, 0.036f};

        // ---- the crate's environment ----
        bool  capture  = true;
        int   envSize  = 256;
        float envMix   = 0.18f;   // low by request: at 0.5 the box stops being wood
        float ior      = 1.45f;   // -> uEta, and the reflection's F0 is not modelled
        float refract  = 1.0f;    // 0 = mirror only, 1 = bend what faces you
        float envLod   = 1.5f;    // wood is not a mirror; mip 1.5 of a 256 cube
        float emissive = 4.2f;    // lantern glass, above 1 so bloom keys on it

        // ---- pipeline ----
        bool cull = true;
        bool wireframe = false;
    };
    Settings settings;

    void Execute(gldx::RenderFrame& f) override;

    // ---- evidence for the HUD ----------------------------------------------
    // What the crate actually sampled on the last frame. Printing the boolean the
    // settings object holds would be printing an intention; this is the texture
    // unit that was bound, which is the thing that can disagree with it.
    [[nodiscard]] bool captureLive() const noexcept { return capturedOnce_ && liveCube_ == kCaptured; }
    // Faces rendered into the cube on the last frame. Six means a fresh capture;
    // zero with captureLive() true means the crate did not move and the previous
    // frame's cube was reused, which is the whole point of `lastCenter_`.
    [[nodiscard]] int  facesCaptured() const noexcept { return facesLastFrame_; }
    // The main scene only - the capture's own submissions are reported separately,
    // because "five draws for a forest" is the demo's claim and adding six times
    // that to the same number would argue against it.
    [[nodiscard]] int  drawsLastFrame() const noexcept { return drawsLastFrame_; }
    [[nodiscard]] int  captureDrawsLastFrame() const noexcept { return captureDrawsLastFrame_; }
    [[nodiscard]] std::uint32_t instancesLastFrame() const noexcept { return instLastFrame_; }
    [[nodiscard]] int  envSizeLive() const noexcept { return envSizeBuilt_; }
    [[nodiscard]] const char* envSource() const noexcept {
        return liveCube_ == kCaptured ? "FBO capture" : "star cube (fallback)";
    }

private:
    enum CubeSource { kNone = 0, kStar, kCaptured };

    void OpenSceneTarget(gldx::RenderFrame& f);
    void CaptureEnvironment(gldx::RenderFrame& f, const glm::vec3& center);
    // The world minus the crate, or the world as the window sees it - sky included,
    // because a reflection of a scene with no background in it is a black cube and
    // the moon has to come back in the crate's top face for the face table to be
    // visibly the same one Sky.cpp painted with. There is no eye parameter: fog and
    // the specular's V are read out of the lighting UBO, so whoever calls this owns
    // the job of having put the right eye in it first (see Execute).
    void DrawWorld(const glm::mat4& viewProj, const glm::mat4& skyViewProj,
                   bool includeCrate);
    void EnsureCaptureTargets(int size);

    gldx::ShaderProgram   prog_;
    gldx::TextureCubeMap  capture_;
    gldx::RenderTexture   captureDepth_;
    gldx::Framebuffer     captureFbo_;
    int    envSizeBuilt_ = 0;
    bool   capturedOnce_ = false;
    bool   captureFailed_ = false;
    CubeSource liveCube_ = kNone;
    int    facesLastFrame_ = 0;
    int    drawsLastFrame_ = 0;
    int    captureDrawsLastFrame_ = 0;
    std::uint32_t instLastFrame_ = 0;
    // DrawWorld's own tally; Execute zeroes it before each of the two phases and
    // reads it back after, so one function can serve six faces and one window
    // without either caller having to know how many objects it drew.
    int           tallyDraws_ = 0;
    std::uint32_t tallyInst_  = 0;
    glm::vec3 lastCenter_{0.0f, 1e9f, 0.0f};   // a sentinel no pose can equal
};

} // namespace night_forest

#endif // NIGHT_FOREST_PASSES_H
