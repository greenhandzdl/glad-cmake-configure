module;

#include "gldx/gmf.hpp"

#include <stb_image.h>

module gldx;

namespace gldx {

std::expected<Texture2DDesc, std::string> LoadImageToDesc(std::string_view path,
                                                           int forceChannels) {
    const std::string file(path);
    // Look at the header before letting stb allocate anything. stb guards a
    // decode at roughly a gigabyte of pixels, which is far above what the GL
    // side will take: a 144 KB PNG claiming 16500x3000 used to reach this
    // function, get its 148 MB decoded, get copied into a vector (peak 600 MB
    // measured), and only then be thrown out by Texture2D::Upload's size check.
    // Refusing on the header costs one extra open for every real image and puts
    // the amplification back to one to one.
    int headerW = 0, headerH = 0, headerChannels = 0;
    if (stbi_info(file.c_str(), &headerW, &headerH, &headerChannels)
        && (headerW > kMaxTextureSide || headerH > kMaxTextureSide)) {
        return std::unexpected("image too large: " + file + " is " + std::to_string(headerW)
                               + "x" + std::to_string(headerH) + ", the limit is "
                               + std::to_string(kMaxTextureSide));
    }

    int w = 0, h = 0, ch = 0;
    stbi_uc* data = stbi_load(file.c_str(), &w, &h, &ch, forceChannels);
    if (!data) {
        return std::unexpected(std::string("stbi_load failed: ") + file);
    }
    Texture2DDesc desc;
    desc.width = w;
    desc.height = h;
    desc.channels = forceChannels > 0 ? forceChannels : ch;
    const std::size_t bytes = static_cast<std::size_t>(w) * h * desc.channels;
    desc.pixels.assign(data, data + bytes);
    stbi_image_free(data);   // always released, both this path and none earlier
    return desc;
}

} // namespace gldx
