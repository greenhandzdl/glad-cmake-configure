module;

#include "gfx/gmf.hpp"

#include <stb_image.h>

module gfx;

namespace gfx {

std::expected<Texture2DDesc, std::string> LoadImageToDesc(std::string_view path,
                                                           int forceChannels) {
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
    stbi_image_free(data);   // always released, both this path and none earlier
    return desc;
}

} // namespace gfx
