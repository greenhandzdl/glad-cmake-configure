#ifndef GFX_ASSETS_IMAGELOADER_H
#define GFX_ASSETS_IMAGELOADER_H

/**
 * @file ImageLoader.h
 * @brief CPU-only image decoding to Texture2DDesc (Stage A).
 *
 * Safe to call from worker threads; no GL is touched. STB is an implementation
 * detail kept in ImageLoader.cpp (the STB_IMAGE_IMPLEMENTATION macro itself
 * lives in src/gfx/third_party/stb_image_impl.cpp), so this public header - and
 * the `gfx` module interface that exports it - never leaks <stb_image.h>.
 */

#include <expected>
#include <string>
#include <string_view>

#include "gfx/texture/Texture2D.h"

namespace gfx {

// Decode an image file into a CPU-side Texture2DDesc (RGBA/RGB as requested via
// forceChannels, 0 = keep the file's channel count). Returns an error string on
// decode failure. Runs on a worker thread.
std::expected<Texture2DDesc, std::string> LoadImageToDesc(std::string_view path,
                                                           int forceChannels = 0);

} // namespace gfx

#endif // GFX_ASSETS_IMAGELOADER_H
