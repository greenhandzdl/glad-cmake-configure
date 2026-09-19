#include "gfx/texture/TextureCubeMap.h"

#include <cmath>
#include <utility>

#include "gfx/core/RenderContext.h"

namespace gfx {

namespace {
// Cube face targets in the canonical order +X -X +Y -Y +Z -Z.
constexpr GLenum kFaceTargets[6] = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};
} // namespace

TextureCubeMap::~TextureCubeMap() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~TextureCubeMap");
        glDeleteTextures(1, &id_);
    }
}

TextureCubeMap::TextureCubeMap(TextureCubeMap&& other) noexcept
    : id_(other.id_), size_(other.size_), levels_(other.levels_) {
    other.id_ = 0;
}

TextureCubeMap& TextureCubeMap::operator=(TextureCubeMap&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            RenderContext::AssertRenderThread("TextureCubeMap::move=");
            glDeleteTextures(1, &id_);
        }
        id_ = other.id_;
        size_ = other.size_;
        levels_ = other.levels_;
        other.id_ = 0;
    }
    return *this;
}

void TextureCubeMap::Upload(const TextureCubeDesc& desc) {
    RenderContext::AssertRenderThread("TextureCubeMap::Upload");
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    if (desc.size <= 0) return;

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    size_ = desc.size;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (desc.hdr) {
        const GLenum internal = GL_RGB16F;
        const std::size_t faceHalfs = static_cast<std::size_t>(desc.size) * desc.size * 4;
        for (int f = 0; f < 6; ++f) {
            const std::uint16_t* face =
                desc.half.empty() ? nullptr : desc.half.data() + static_cast<std::size_t>(f) * faceHalfs;
            glTexImage2D(kFaceTargets[f], 0, internal, desc.size, desc.size, 0,
                         GL_RGBA, GL_HALF_FLOAT, face);
        }
    } else {
        const GLenum internal = desc.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        const std::size_t faceBytes = static_cast<std::size_t>(desc.size) * desc.size * 4;
        for (int f = 0; f < 6; ++f) {
            const std::uint8_t* face =
                desc.rgba.empty() ? nullptr : desc.rgba.data() + static_cast<std::size_t>(f) * faceBytes;
            glTexImage2D(kFaceTargets[f], 0, internal, desc.size, desc.size, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, face);
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    levels_ = 1;
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void TextureCubeMap::AllocateCube(GLenum internalFormat, int size, int levels,
                                  GLenum format, GLenum type) {
    RenderContext::AssertRenderThread("TextureCubeMap::AllocateCube");
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    size_ = size;
    levels_ = levels;
    // glTexStorage2D allocates the whole cube store (all 6 faces x all mips) in
    // one call against the GL_TEXTURE_CUBE_MAP target; per-face targets are not
    // valid storage targets and would leave the cube incomplete.
    glTexStorage2D(GL_TEXTURE_CUBE_MAP, levels, internalFormat, size, size);
    // A single-level cube must not request a mip-mapped min filter, or the
    // texture is incomplete (Apple drivers then sample it as zero).
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    (void)format;
    (void)type;
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void TextureCubeMap::GenerateMipmaps() {
    RenderContext::AssertRenderThread("TextureCubeMap::GenerateMipmaps");
    if (id_ == 0) return;
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    levels_ = static_cast<int>(std::floor(std::log2(static_cast<float>(size_)))) + 1;
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

void TextureCubeMap::Bind(unsigned slot) const {
    RenderContext::AssertRenderThread("TextureCubeMap::Bind");
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_);
}

void TextureCubeMap::Unbind(unsigned slot) {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}

} // namespace gfx
