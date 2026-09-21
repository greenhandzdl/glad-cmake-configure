module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

namespace {
// Anisotropic filtering tokens come from GL_ARB_texture_filter_anisotropic
// (core in 4.6). Guard so builds against plain 4.1 headers still compile; the
// descriptor simply ignores anisotropy when the tokens are unavailable.
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FEu
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FEu
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FFu
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FFu
#endif
} // namespace

Sampler::~Sampler() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~Sampler");
        glDeleteSamplers(1, &id_);
    }
}

Sampler::Sampler(Sampler&& other) noexcept : id_(other.id_) {
    other.id_ = 0;
}

Sampler& Sampler::operator=(Sampler&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            RenderContext::AssertRenderThread("Sampler::move=");
            glDeleteSamplers(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void Sampler::Create(const Desc& desc) {
    RenderContext::AssertRenderThread("Sampler::Create");
    if (id_ != 0) {
        glDeleteSamplers(1, &id_);
        id_ = 0;
    }
    glGenSamplers(1, &id_);
    glSamplerParameteri(id_, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(desc.minFilter));
    glSamplerParameteri(id_, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(desc.magFilter));
    glSamplerParameteri(id_, GL_TEXTURE_WRAP_S, static_cast<GLint>(desc.wrapS));
    glSamplerParameteri(id_, GL_TEXTURE_WRAP_T, static_cast<GLint>(desc.wrapT));
    glSamplerParameterf(id_, GL_TEXTURE_MIN_LOD, desc.minLod);
    glSamplerParameterf(id_, GL_TEXTURE_MAX_LOD, desc.maxLod);
    if (desc.maxAnisotropy > 1.0f) {
        glSamplerParameterf(id_, GL_TEXTURE_MAX_ANISOTROPY, desc.maxAnisotropy);
    }
    if (desc.compareMode) {
        glSamplerParameteri(id_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glSamplerParameteri(id_, GL_TEXTURE_COMPARE_FUNC, static_cast<GLint>(desc.compareFunc));
    }
}

void Sampler::Bind(GLuint unit) const {
    RenderContext::AssertRenderThread("Sampler::Bind");
    glBindSampler(unit, id_);
}

void Sampler::Unbind(GLuint unit) {
    glBindSampler(unit, 0);
}

} // namespace gldx
