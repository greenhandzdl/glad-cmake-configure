#ifndef GFX_TEXTURE_RENDERTEXTURE_H
#define GFX_TEXTURE_RENDERTEXTURE_H

/**
 * @file RenderTexture.h
 * @brief GPU texture allocated empty and used as a framebuffer attachment.
 *
 * Covers the Phase 2 offscreen targets: HDR colour (RGBA16F), depth
 * (DEPTH_COMPONENT24/32) and — when `layers` > 1 — a 2D texture array so all
 * cascaded shadow maps live in one GL_TEXTURE_2D_ARRAY sampled by
 * sampler2DArrayShadow. Render-thread only (allocation touches GL).
 */

#include <glad/gl.h>

namespace gfx {

class RenderTexture {
public:
    enum class Format {
        Rgba16F,     // GL_RGBA16F colour (GL_TEXTURE_2D)
        Depth24,     // GL_DEPTH_COMPONENT24
        Depth32F,    // GL_DEPTH_COMPONENT32F
    };

    RenderTexture() = default;
    ~RenderTexture();

    RenderTexture(const RenderTexture&)            = delete;
    RenderTexture& operator=(const RenderTexture&) = delete;
    RenderTexture(RenderTexture&& other) noexcept;
    RenderTexture& operator=(RenderTexture&& other) noexcept;

    // Allocate the store. layers > 1 => GL_TEXTURE_2D_ARRAY. Render-thread only.
    void Allocate(Format fmt, int width, int height, int layers = 1, bool generateMips = false);

    void Bind(unsigned slot = 0) const;   // binds to 2D or 2D_ARRAY per target
    static void Unbind(unsigned slot);

    [[nodiscard]] GLuint target() const noexcept { return target_; }
    [[nodiscard]] GLuint id()     const noexcept { return id_; }
    [[nodiscard]] int    width()  const noexcept { return width_; }
    [[nodiscard]] int    height() const noexcept { return height_; }
    [[nodiscard]] int    layers() const noexcept { return layers_; }
    [[nodiscard]] bool   valid()  const noexcept { return id_ != 0; }

private:
    void Delete();

    GLuint id_ = 0;
    GLuint target_ = GL_TEXTURE_2D;
    int width_ = 0;
    int height_ = 0;
    int layers_ = 1;
    Format fmt_ = Format::Rgba16F;
};

} // namespace gfx

#endif // GFX_TEXTURE_RENDERTEXTURE_H
