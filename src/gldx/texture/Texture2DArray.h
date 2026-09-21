#ifndef GLDX_TEXTURE_TEXTURE2DARRAY_H
#define GLDX_TEXTURE_TEXTURE2DARRAY_H

/**
 * @file Texture2DArray.h
 * @brief RAII, move-only 2D texture array (GL_TEXTURE_2D_ARRAY) resource.
 *
 * The voxel path's block atlas of choice: one equally-sized RGBA8/RGB8 slice
 * per block type, sampled in GLSL as texture(sampler2DArray, vec3(uv, layer)).
 * Layer isolation removes the atlas bleeding that a packed 2D atlas needs
 * padding/inset tricks to hide, and gives free per-slice mipmapping.
 *
 * Same two-phase ownership as Texture2D: the desc is decoded/assembled on
 * worker threads (Stage A), Upload() creates the GL texture on the render
 * thread (Stage B). All layers share width/height/channels.
 */

#include <cstdint>
#include <vector>

#include <glad/gl.h>

namespace gldx {

struct Texture2DArrayDesc {
    int width = 0;
    int height = 0;
    int layers = 0;                   // >= 1
    int channels = 4;                 // 1..4, identical for every layer
    bool srgb = true;                 // color slices use sRGB; data slices set false
    std::vector<std::uint8_t> pixels; // layers concatenated, each width*height*channels
};

class Texture2DArray {
public:
    Texture2DArray() = default;
    ~Texture2DArray();

    Texture2DArray(const Texture2DArray&)            = delete;
    Texture2DArray& operator=(const Texture2DArray&) = delete;
    Texture2DArray(Texture2DArray&& other) noexcept;
    Texture2DArray& operator=(Texture2DArray&& other) noexcept;

    // Stage B: create the GL texture array from the packed layers. Render-thread only.
    void Upload(const Texture2DArrayDesc& desc);

    // Bind to a texture unit (default unit 0) and return the unit used.
    void Bind(unsigned slot = 0) const;
    static void Unbind(unsigned slot);

    [[nodiscard]] GLuint id()       const noexcept { return id_; }
    [[nodiscard]] int    width()    const noexcept { return width_; }
    [[nodiscard]] int    height()   const noexcept { return height_; }
    [[nodiscard]] int    layers()   const noexcept { return layers_; }
    [[nodiscard]] bool   valid()    const noexcept { return id_ != 0; }

private:
    GLuint id_ = 0;
    int width_  = 0;
    int height_ = 0;
    int layers_ = 0;
};

} // namespace gldx

#endif // GLDX_TEXTURE_TEXTURE2DARRAY_H
