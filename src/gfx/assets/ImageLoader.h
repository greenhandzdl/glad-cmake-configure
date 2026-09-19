#ifndef GFX_ASSETS_IMAGELOADER_H
#define GFX_ASSETS_IMAGELOADER_H

/**
 * @file ImageLoader.h
 * @brief CPU-only image decoding to Texture2DDesc (Stage A).
 *
 * Wraps stb_image; safe to call from worker threads. The implementation macro
 * STB_IMAGE_IMPLEMENTATION lives in src/utils/stb.cpp, so this TU only sees the
 * declarations. No GL is touched here.
 */

#include <expected>
#include <string>
#include <string_view>

#include <stb_image.h>

#include "gfx/texture/Texture2D.h"

namespace gfx {

inline std::expected<Texture2DDesc, std::string> LoadImageToDesc(std::string_view path,
                                                                  int forceChannels = 0) {
    int w = 0, h = 0, ch = 0;
    stbi_uc* data = stbi_load(std::string(path).c_str(), &w, &h, &ch, forceChannels);
    if (!data) {
        return std::unexpected(std::string("stbi_load failed: ") + std::string(path));
    }
    Texture2DDesc desc;
    desc.width = w;
    desc.height = h;
    desc.channels = forceChannels > 0 ? forceChannels : ch;
    const std::size_t bytes = static_cast<std::size_t>(w) * h * desc.channels;
    desc.pixels.assign(data, data + bytes);
    stbi_image_free(data);
    return desc;
}

} // namespace gfx

#endif // GFX_ASSETS_IMAGELOADER_H
