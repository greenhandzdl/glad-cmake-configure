#ifndef GFX_TEXTURE_TEXTURE2D_H
#define GFX_TEXTURE_TEXTURE2D_H

/**
 * @file Texture2D.h
 * @brief RAII, move-only 2D texture resource with a CPU-side staging descriptor.
 *
 * Texture2DDesc is produced on worker threads (Stage A, e.g. by ImageLoader
 * via stb_image). Texture2D::Upload performs glTexImage2D on the render thread
 * (Stage B). sRGB internal format is chosen for color textures (PBR-correct).
 */

#include <cstdint>
#include <vector>

#include <glad/gl.h>

namespace gfx {

struct Texture2DDesc {
    int width = 0;
    int height = 0;
    int channels = 4;                 // 1..4 as decoded by stb
    bool srgb = true;                 // color maps use sRGB; data maps set false
    std::vector<std::uint8_t> pixels; // row-major, `channels` bytes per texel
};

class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();

    Texture2D(const Texture2D&)            = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    // Stage B: create the GL texture from decoded pixels. Render-thread only.
    void Upload(const Texture2DDesc& desc);

    // Bind to a texture unit (default unit 0) and return the unit used.
    void Bind(unsigned slot = 0) const;
    static void Unbind(unsigned slot);

    [[nodiscard]] GLuint id()       const noexcept { return id_; }
    [[nodiscard]] int    width()    const noexcept { return width_; }
    [[nodiscard]] int    height()   const noexcept { return height_; }
    [[nodiscard]] bool   valid()    const noexcept { return id_ != 0; }

private:
    GLuint id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

} // namespace gfx

#endif // GFX_TEXTURE_TEXTURE2D_H
