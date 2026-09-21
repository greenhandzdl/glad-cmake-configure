module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

namespace {
struct GlFormat { GLenum internal; GLenum format; GLenum type; };
GlFormat Resolve(RenderTexture::Format f) {
    switch (f) {
        case RenderTexture::Format::Depth24:  return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT};
        case RenderTexture::Format::Depth32F: return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
        case RenderTexture::Format::Rgba16F:
        default:                              return {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT};
    }
}
} // namespace

RenderTexture::~RenderTexture() { Delete(); }

void RenderTexture::Delete() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~RenderTexture");
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
}

RenderTexture::RenderTexture(RenderTexture&& other) noexcept
    : id_(other.id_), target_(other.target_),
      width_(other.width_), height_(other.height_), layers_(other.layers_),
      samples_(other.samples_), fmt_(other.fmt_) {
    other.id_ = 0;
}

RenderTexture& RenderTexture::operator=(RenderTexture&& other) noexcept {
    if (this != &other) {
        Delete();
        id_ = other.id_;
        target_ = other.target_;
        width_ = other.width_;
        height_ = other.height_;
        layers_ = other.layers_;
        samples_ = other.samples_;
        fmt_ = other.fmt_;
        other.id_ = 0;
    }
    return *this;
}

void RenderTexture::Allocate(Format fmt, int width, int height, int layers, bool generateMips) {
    RenderContext::AssertRenderThread("RenderTexture::Allocate");
    Delete();
    fmt_ = fmt;
    width_ = width;
    height_ = height;
    layers_ = layers < 1 ? 1 : layers;
    target_ = (layers_ > 1) ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;

    const auto [internal, format, type] = Resolve(fmt);
    glGenTextures(1, &id_);
    glBindTexture(target_, id_);

    const int levels = generateMips ? 1 + static_cast<int>(std::log2f(static_cast<float>(width > height ? width : height)))
                                    : 1;
    if (target_ == GL_TEXTURE_2D_ARRAY) {
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, levels > 0 ? levels : 1, internal, width, height, layers_);
    } else {
        glTexStorage2D(GL_TEXTURE_2D, levels > 0 ? levels : 1, internal, width, height);
    }

    const bool depth = (fmt == Format::Depth24 || fmt == Format::Depth32F);
    const GLenum filter = depth ? GL_LINEAR : GL_LINEAR;
    glTexParameteri(target_, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(target_, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(target_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    (void)format;
    (void)type;
    glBindTexture(target_, 0);
}

void RenderTexture::AllocateMultisample(Format fmt, int width, int height, int samples) {
    RenderContext::AssertRenderThread("RenderTexture::AllocateMultisample");
    Delete();
    fmt_ = fmt;
    width_ = width;
    height_ = height;
    layers_ = 1;
    samples_ = samples < 1 ? 1 : samples;
    target_ = GL_TEXTURE_2D_MULTISAMPLE;

    const auto [internal, format, type] = Resolve(fmt);
    (void)format;
    (void)type;
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, id_);
    // Fixed sample locations so the resolve blit and any post-process depth
    // reads stay consistent across the two MSAA attachments.
    glTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, samples_, internal,
                            width, height, GL_TRUE);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
}

void RenderTexture::Bind(unsigned slot) const {
    RenderContext::AssertRenderThread("RenderTexture::Bind");
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(target_, id_);
}

void RenderTexture::Unbind(unsigned slot) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0);
}

} // namespace gldx
