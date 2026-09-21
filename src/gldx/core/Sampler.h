#ifndef GLDX_CORE_SAMPLER_H
#define GLDX_CORE_SAMPLER_H

/**
 * @file Sampler.h
 * @brief RAII, move-only OpenGL sampler object (separate from a texture).
 *
 * Sampler objects (4.0+) let one texture be sampled with different policies.
 * Phase 2 uses them for cascaded shadow maps (hardware compare + border clamp)
 * and for the IBL prefiltered environment (LOD clamp). All calls are
 * render-thread only.
 */

#include <glad/gl.h>

namespace gldx {

class Sampler {
public:
    struct Desc {
        GLenum minFilter   = GL_LINEAR;
        GLenum magFilter   = GL_LINEAR;
        GLenum wrapS       = GL_CLAMP_TO_EDGE;
        GLenum wrapT       = GL_CLAMP_TO_EDGE;
        float  maxAnisotropy = 1.0f;   // 1.0 disables anisotropic filtering
        // Shadow-map compare: GL_NONE or GL_TEXTURE_COMPARE_MODE/func.
        bool   compareMode = false;
        GLenum compareFunc = GL_LEQUAL;
        float  minLod      = 0.0f;
        float  maxLod      = 1000.0f;  // large default => full mip range
    };

    Sampler() = default;
    ~Sampler();

    Sampler(const Sampler&)            = delete;
    Sampler& operator=(const Sampler&) = delete;
    Sampler(Sampler&& other) noexcept;
    Sampler& operator=(Sampler&& other) noexcept;

    void Create(const Desc& desc);   // glGenSamplers + set params (render thread)
    void Bind(GLuint unit) const;    // glBindSampler(unit, id_)
    static void Unbind(GLuint unit);

    [[nodiscard]] GLuint id()    const noexcept { return id_; }
    [[nodiscard]] bool   valid() const noexcept { return id_ != 0; }

private:
    GLuint id_ = 0;
};

} // namespace gldx

#endif // GLDX_CORE_SAMPLER_H
