#ifndef GFX_RENDER_POSTPROCESSCHAIN_H
#define GFX_RENDER_POSTPROCESSCHAIN_H

/**
 * @file PostProcessChain.h
 * @brief HDR + MSAA + Bloom + ACES tone-map chain (plan "PostProcessChain").
 *
 * Frame flow, driven by the owner each frame (all render-thread):
 *   chain.Resize(fbw, fbh, samples);      // once, on (re)size
 *   chain.BeginScene();                   // bind (MSAA) HDR target, clear
 *   ...draw the 3D PBR scene...            // shadow pass may bind its own FBO
 *   chain.EndScene();                     // resolve MSAA -> single-sample HDR
 *   chain.RenderBloom();                  // bright pass + separable blur ping-pong
 *   chain.Composite();                    // HDR + bloom -> ACES -> default FBO
 *   ...draw 2D HUD (SpriteBatch) onto the default FBO...
 *
 * The 3D pass writes linear HDR (PBR/skybox shaders no longer tone-map); the
 * composite pass applies exposure + ACES filmic + gamma, so bloom interacts
 * with real HDR values (e.g. the sun disc).
 */

#include "gfx/render/Framebuffer.h"
#include "gfx/shader/ShaderProgram.h"
#include "gfx/core/VertexArray.h"
#include "gfx/texture/RenderTexture.h"

namespace gfx {

class PostProcessChain {
public:
    // Compile the pass shaders + create the shared fullscreen VAO. Render-thread.
    bool Init();

    // (Re)allocate every offscreen target for the given pixel size. `msaaSamples`
    // is clamped to the driver maximum; <=1 disables MSAA (direct HDR render).
    void Resize(int width, int height, int msaaSamples);

    void BeginScene();
    void EndScene();
    void RenderBloom();
    void Composite();

    void SetExposure(float e) noexcept { exposure_ = e; }
    void SetBloomStrength(float s) noexcept { bloomStrength_ = s; }
    void SetBloomThreshold(float t) noexcept { bloomThreshold_ = t; }
    void SetBloomIterations(int n) noexcept { bloomIterations_ = n < 1 ? 1 : (n > 6 ? 6 : n); }

    [[nodiscard]] bool   msaaEnabled() const noexcept { return msaaSamples_ > 1; }
    [[nodiscard]] int    width()  const noexcept { return width_; }
    [[nodiscard]] int    height() const noexcept { return height_; }
    [[nodiscard]] bool   valid()  const noexcept { return hdrColor_.valid(); }

private:
    void DrawFullscreen();   // binds the empty VAO + issues a 3-vertex triangle

    // Scene targets.
    RenderTexture msaaColor_, msaaDepth_;   // multisampled (only when msaaSamples_ > 1)
    RenderTexture hdrColor_;                // single-sample linear HDR resolve
    Framebuffer   msaaFbo_, hdrFbo_;

    // Bloom targets (half resolution).
    RenderTexture bloomA_, bloomB_;
    Framebuffer   bloomFboA_, bloomFboB_;

    ShaderProgram bright_, blur_, composite_;
    VertexArray   emptyVao_;

    int width_ = 0, height_ = 0;
    int bloomW_ = 0, bloomH_ = 0;
    int msaaSamples_ = 1;
    int maxSamples_ = 1;
    int bloomIterations_ = 2;

    float exposure_ = 1.0f;
    float bloomStrength_ = 0.4f;
    float bloomThreshold_ = 1.0f;
    float bloomSoftKnee_ = 0.5f;
};

} // namespace gfx

#endif // GFX_RENDER_POSTPROCESSCHAIN_H
