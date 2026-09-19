#ifndef GFX_SHADOW_CASCADEDSHADOWMAP_H
#define GFX_SHADOW_CASCADEDSHADOWMAP_H

/**
 * @file CascadedShadowMap.h
 * @brief Directional-light cascaded shadow maps (plan "CascadedShadowMap").
 *
 * Holds all cascades in a single GL_TEXTURE_2D_ARRAY (layer = cascade) sampled
 * by sampler2DArrayShadow, plus a ShadowBlock UBO describing the light-space
 * matrices and split distances. The owner (main loop / Renderer) drives the
 * depth pass:
 *
 *   csm.Update(camera);
 *   for (int i = 0; i < kCascadeCount; ++i) {
 *       csm.BeginCascade(i);
 *       ...draw casters with a depth-only shader...
 *       csm.EndCascade();
 *   }
 *   csm.Upload();       // publish the ShadowBlock UBO for the PBR pass
 *   csm.Bind(unit);     // for sampling in the lighting pass
 *
 * All methods are render-thread only (they allocate / bind GL objects).
 */

#include "gfx/core/Sampler.h"
#include "gfx/core/UniformBuffer.h"
#include "gfx/render/Framebuffer.h"
#include "gfx/shadow/ShadowData.h"
#include "gfx/texture/RenderTexture.h"

namespace gfx {

class Camera;

class CascadedShadowMap {
public:
    static constexpr GLuint kShadowBinding = 2;  // ShadowBlock UBO binding point

    // Allocate the depth array + FBO + sampler. Render-thread only.
    void Init(int resolution = 2048);

    // Recompute split distances and stabilized light-space matrices.
    void Update(const Camera& camera, const glm::vec3& sunDirection);

    // Depth-pass control for one cascade layer.
    void BeginCascade(int cascade);
    void EndCascade();

    // Upload the ShadowBlock UBO (call after all cascades are rendered).
    void Upload();

    // Bind the ShadowBlock UBO to its fixed point for the lighting pass.
    void BindUniform() const;

    // Bind the depth array + compare sampler for the lighting pass.
    void Bind(unsigned unit) const;
    static void Unbind(unsigned unit);

    [[nodiscard]] const ShadowBlockGpu& data() const noexcept { return data_; }
    [[nodiscard]] int resolution() const noexcept { return resolution_; }
    [[nodiscard]] bool valid() const noexcept { return depthArray_.valid(); }

private:
    int resolution_ = 0;
    float lambda_ = 0.95f;   // log/linear blend for split placement

    RenderTexture depthArray_;
    Framebuffer   fbo_;
    Sampler       sampler_;
    UniformBuffer shadowUbo_;

    ShadowBlockGpu data_{};
};

} // namespace gfx

#endif // GFX_SHADOW_CASCADEDSHADOWMAP_H
