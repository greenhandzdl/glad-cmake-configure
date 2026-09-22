#ifndef GLDX_TEXTURE_TEXTUREFORMAT_H
#define GLDX_TEXTURE_TEXTUREFORMAT_H

/**
 * @file TextureFormat.h
 * @brief Shared CPU-channel-count -> GL format mapping for the texture uploads.
 *
 * Texture2D and Texture2DArray feed the same 1..4-channel byte buffers into
 * glTexImage*(D). They historically kept byte-for-byte identical tables (the
 * array copy even carried a "Mirrors Texture2D's table" note) that could silently
 * drift; this is the single copy both module-implementation units include from
 * their global module fragment.
 *
 * Pure GL-enum arithmetic — no state and no GL calls — so it is safe to call
 * from any thread; the uploads that consume it stay render-thread only.
 */

#include <glad/gl.h>

namespace gldx {
namespace detail {

// Pixel layout of the source bytes, chosen from the channel count. Anything
// outside 1..4 is treated as RGBA, so callers must validate the range first.
inline GLenum DataFormat(int channels) {
    switch (channels) {
        case 1: return GL_RED;
        case 2: return GL_RG;
        case 3: return GL_RGB;
        default: return GL_RGBA;
    }
}

// Storage format on the GPU. There is no sRGB single/two-channel format, so
// those two stay linear even when srgb is requested.
inline GLenum InternalFormat(int channels, bool srgb) {
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

} // namespace detail
} // namespace gldx

#endif // GLDX_TEXTURE_TEXTUREFORMAT_H
