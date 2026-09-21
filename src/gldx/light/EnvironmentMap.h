#ifndef GLDX_LIGHT_ENVIRONMENTMAP_H
#define GLDX_LIGHT_ENVIRONMENTMAP_H

/**
 * @file EnvironmentMap.h
 * @brief HDR sky cube + its image-based-lighting precomputations (plan IBL).
 *
 * Owns four GPU resources and the shaders that bake them, all on the render
 * thread in Generate():
 *   - sky_:        procedural HDR cube (also the skybox background + IBL source)
 *   - irradiance_: cosine-convolved diffuse cube
 *   - prefilter_:  GGX-prefiltered specular cube (roughness encoded in mip)
 *   - brdf_:       2D split-sum BRDF integration LUT
 *
 * Generation is one-time at startup; afterwards the maps are read-only, so
 * binding them for the PBR pass is thread-safe by construction.
 */

#include <glm/glm.hpp>

#include "gldx/core/VertexArray.h"
#include "gldx/render/Framebuffer.h"
#include "gldx/shader/ShaderProgram.h"
#include "gldx/texture/RenderTexture.h"
#include "gldx/texture/TextureCubeMap.h"

namespace gldx {

class EnvironmentMap {
public:
    // Bake all maps from a procedural sky lit by `sunDir` (travel direction).
    // Render-thread only. Returns false (and logs) if any shader failed.
    bool Generate(const glm::vec3& sunDir,
                  int skySize = 256, int irradianceSize = 32, int prefilterSize = 256);

    [[nodiscard]] bool valid() const noexcept { return sky_.valid() && irradiance_.valid()
                                                   && prefilter_.valid() && brdf_.valid(); }

    // Bind for the PBR lighting pass (each advances `unit` by one).
    void BindIrradiance(unsigned& unit) const;
    void BindPrefilter(unsigned& unit) const;
    void BindBrdf(unsigned& unit) const;
    void BindSky(unsigned& unit) const;   // for the skybox background

    [[nodiscard]] const TextureCubeMap& sky()        const noexcept { return sky_; }
    [[nodiscard]] const TextureCubeMap& prefilter()  const noexcept { return prefilter_; }
    [[nodiscard]] int prefilterMips() const noexcept { return prefilterMips_; }

private:
    bool Compile();
    void RenderSky();
    void RenderIrradiance();
    void RenderPrefilter();
    void RenderBrdf();

    TextureCubeMap sky_;
    TextureCubeMap irradiance_;
    TextureCubeMap prefilter_;
    RenderTexture  brdf_;

    ShaderProgram skyGenShader_, irradianceShader_, prefilterShader_, brdfShader_;
    VertexArray   emptyVao_;   // attribute-less VAO for fullscreen-triangle draws
    Framebuffer   fbo_;

    glm::vec3 sunDir_{0.0f, -1.0f, 0.0f};
    int skySize_ = 256;
    int irradianceSize_ = 32;
    int prefilterSize_ = 256;
    int prefilterMips_ = 1;
};

} // namespace gldx

#endif // GLDX_LIGHT_ENVIRONMENTMAP_H
