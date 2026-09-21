module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

namespace {
// Sides are bounded by kMaxTextureSide (Texture2D.h), the same bound the 2D
// path and the image loader use. Layers get their own bound, and the total
// byte count is still checked with division below rather than by multiplying.
constexpr int kMaxTextureLayers = 4096;
GLenum ArrayDataFormat(int channels) {
    switch (channels) {
        case 1: return GL_RED;
        case 2: return GL_RG;
        case 3: return GL_RGB;
        default: return GL_RGBA;
    }
}
GLenum ArrayInternalFormat(int channels, bool srgb) {
    // Mirrors Texture2D's table: there is no sRGB single/two-channel format.
    if (srgb) {
        switch (channels) {
            case 1: return GL_R8;
            case 2: return GL_RG8;
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

Texture2DArray::~Texture2DArray() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~Texture2DArray");
        glDeleteTextures(1, &id_);
    }
}

Texture2DArray::Texture2DArray(Texture2DArray&& other) noexcept
    : id_(other.id_), width_(other.width_), height_(other.height_), layers_(other.layers_) {
    other.id_ = 0;
}

Texture2DArray& Texture2DArray::operator=(Texture2DArray&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            RenderContext::AssertRenderThread("Texture2DArray::move=");
            glDeleteTextures(1, &id_);
        }
        id_ = other.id_; width_ = other.width_; height_ = other.height_; layers_ = other.layers_;
        other.id_ = 0;
    }
    return *this;
}

void Texture2DArray::Upload(const Texture2DArrayDesc& desc) {
    RenderContext::AssertRenderThread("Texture2DArray::Upload");
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    // A description is only usable when its byte count can be checked against
    // the pixel vector, so the channel count has to be one the formats below
    // actually map to (0 would divide by zero, >4 would upload as RGBA while
    // being measured as something else).
    if (desc.pixels.empty() || desc.width <= 0 || desc.height <= 0 || desc.layers <= 0
        || desc.channels < 1 || desc.channels > 4
        || desc.width > kMaxTextureSide || desc.height > kMaxTextureSide
        || desc.layers > kMaxTextureLayers) {
        return;
    }
    const std::size_t layerBytes =
        static_cast<std::size_t>(desc.width) * desc.height * desc.channels;
    // Division rather than `layerBytes * layers`: the product of a hostile
    // description can wrap back into range and talk glTexImage3D into reading
    // past the caller's buffer, a quotient cannot.
    if (desc.pixels.size() / layerBytes < static_cast<std::size_t>(desc.layers)) {
        return;
    }

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, id_);
    width_  = desc.width;
    height_ = desc.height;
    layers_ = desc.layers;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0,
                 ArrayInternalFormat(desc.channels, desc.srgb),
                 desc.width, desc.height, desc.layers, 0,
                 ArrayDataFormat(desc.channels), GL_UNSIGNED_BYTE,
                 desc.pixels.data());

    // Full mipmap chain: voxel terrain lives at odd distances from the camera,
    // and NEAREST-style shimmer without mips is very visible on block edges.
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);  // crisp voxels
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Texture2DArray::Bind(unsigned slot) const {
    RenderContext::AssertRenderThread("Texture2DArray::Bind");
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D_ARRAY, id_);
}

void Texture2DArray::Unbind(unsigned slot) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

} // namespace gldx
