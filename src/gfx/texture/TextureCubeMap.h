#ifndef GFX_TEXTURE_TEXTURECUBEMAP_H
#define GFX_TEXTURE_TEXTURECUBEMAP_H

/**
 * @file TextureCubeMap.h
 * @brief RAII, move-only cube-map texture (skybox + IBL environment).
 *
 * Supports both 8-bit sRGB/RGBE-free colour faces and 16-bit float HDR faces
 * (needed as the IBL source). Faces are staged CPU-side as a CubeDesc (Stage A,
 * 6 x RGBA/Half rows) and uploaded on the render thread (Stage B).
 */

#include <cstdint>
#include <vector>

#include <glad/gl.h>

namespace gfx {

struct TextureCubeDesc {
    int  size = 0;              // square faces
    bool hdr  = false;         // true => GL_RGB16F faces from halfData
    bool srgb = false;         // colour LDR faces use sRGB internal format
    std::vector<std::uint8_t> rgba;  // 6 * size*size * 4 bytes (LDR path)
    std::vector<std::uint16_t> half; // 6 * size*size * 4 halfs (HDR path)
};

class TextureCubeMap {
public:
    TextureCubeMap() = default;
    ~TextureCubeMap();

    TextureCubeMap(const TextureCubeMap&)            = delete;
    TextureCubeMap& operator=(const TextureCubeMap&) = delete;
    TextureCubeMap(TextureCubeMap&& other) noexcept;
    TextureCubeMap& operator=(TextureCubeMap&& other) noexcept;

    void Upload(const TextureCubeDesc& desc);   // render-thread only

    // Allocate an empty cube (render target for IBL generation). Render-thread.
    void AllocateCube(GLenum internalFormat, int size, int levels, GLenum format, GLenum type);

    void GenerateMipmaps();                      // render-thread only
    void Bind(unsigned slot = 0) const;
    static void Unbind(unsigned slot);

    [[nodiscard]] GLuint id()    const noexcept { return id_; }
    [[nodiscard]] int    size()  const noexcept { return size_; }
    [[nodiscard]] int    levels()const noexcept { return levels_; }
    [[nodiscard]] bool   valid() const noexcept { return id_ != 0; }

private:
    GLuint id_ = 0;
    int size_ = 0;
    int levels_ = 1;
};

} // namespace gfx

#endif // GFX_TEXTURE_TEXTURECUBEMAP_H
