module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

namespace {
// Upload() rejects anything wider than this (see the size check below).
constexpr int kMaxTextureSide = 16384;
GLenum DataFormat(int channels) {
    switch (channels) {
        case 1: return GL_RED;
        case 2: return GL_RG;
        case 3: return GL_RGB;
        default: return GL_RGBA;
    }
}
GLenum InternalFormat(int channels, bool srgb) {
    if (srgb) {
        switch (channels) {
            case 1: return GL_R8;                 // no sRGB single channel
            case 2: return GL_RG8;                // no sRGB two channel
            case 3: return GL_SRGB8;
            default: return GL_SRGB8_ALPHA8;
        }
    }
    switch (channels) {
        case 1: return GL_R8;
        case 2: return GL_RG8;
        case 3: return GL_RGB8;
        default: return GL_RGBA8;
    }
}
} // namespace

Texture2D::~Texture2D() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~Texture2D");
        glDeleteTextures(1, &id_);
    }
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : id_(other.id_), width_(other.width_), height_(other.height_) {
    other.id_ = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            RenderContext::AssertRenderThread("Texture2D::move=");
            glDeleteTextures(1, &id_);
        }
        id_ = other.id_; width_ = other.width_; height_ = other.height_;
        other.id_ = 0;
    }
    return *this;
}

void Texture2D::Upload(const Texture2DDesc& desc) {
    RenderContext::AssertRenderThread("Texture2D::Upload");
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    // Same contract as Texture2DArray::Upload: glTexImage2D is handed
    // desc.pixels.data() and reads width*height*channels bytes from it, so the
    // vector has to be proven at least that long first. The channel bounds are
    // part of the check because DataFormat() maps anything else onto RGBA, and
    // the side bound keeps the byte product far away from wrapping (no desktop
    // GL of the 4.1 era samples a larger texture either).
    if (desc.width <= 0 || desc.height <= 0 || desc.channels < 1 || desc.channels > 4
        || desc.width > kMaxTextureSide || desc.height > kMaxTextureSide) {
        return;
    }
    const std::size_t bytes =
        static_cast<std::size_t>(desc.width) * desc.height * desc.channels;
    if (desc.pixels.size() < bytes) return;

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    width_ = desc.width;
    height_ = desc.height;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const GLenum format = DataFormat(desc.channels);
    const GLenum internal = InternalFormat(desc.channels, desc.srgb);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, desc.width, desc.height, 0,
                 format, GL_UNSIGNED_BYTE, desc.pixels.data());

    // Phase 1 default sampling; a dedicated Sampler object (plan) can override.
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::Bind(unsigned slot) const {
    RenderContext::AssertRenderThread("Texture2D::Bind");
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, id_);
}

void Texture2D::Unbind(unsigned slot) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace gfx
