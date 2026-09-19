#include "gfx/text/Font.h"

#include <cstring>
#include <fstream>
#include <vector>

#include <stb_truetype.h>

#include "gfx/core/RenderContext.h"

namespace gfx {

namespace {
// 1024^2 leaves room for the full printable ASCII range at 48px with 2x2
// oversampling (which roughly doubles each glyph's footprint in the atlas).
constexpr int kAtlasW = 1024;
constexpr int kAtlasH = 1024;
} // namespace

std::expected<void, std::string> Font::LoadFromFile(const std::string& path, float pixelSize) {
    RenderContext::AssertRenderThread("Font::LoadFromFile");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::unexpected("Font: cannot open " + path);
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
        return std::unexpected("Font: cannot read " + path);

    stbtt_fontinfo font;
    const int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
    if (!stbtt_InitFont(&font, data.data(), offset))
        return std::unexpected("Font: unrecognised font file " + path);

    // Stage A: pack the printable ASCII range into a single-channel coverage atlas.
    std::vector<unsigned char> coverage(kAtlasW * kAtlasH, 0);
    stbtt_packedchar baked[kNumGlyphs];
    std::memset(baked, 0, sizeof(baked));

    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, coverage.data(), kAtlasW, kAtlasH, 0, 1, nullptr))
        return std::unexpected("Font: atlas PackBegin failed");
    stbtt_PackSetOversampling(&pc, 2, 2);
    const int bakedSize = static_cast<int>(pixelSize);
    if (!stbtt_PackFontRange(&pc, data.data(), 0, static_cast<float>(bakedSize),
                             kFirstChar, kNumGlyphs, baked)) {
        stbtt_PackEnd(&pc);
        return std::unexpected("Font: atlas overflow (glyphs do not fit)");
    }
    stbtt_PackEnd(&pc);

    bakeSize_ = static_cast<float>(bakedSize);
    int a, d, lg;
    stbtt_GetFontVMetrics(&font, &a, &d, &lg);
    const float scale = stbtt_ScaleForPixelHeight(&font, bakeSize_);
    ascent_   = a * scale;
    lineGap_  = (a - d + lg) * scale;   // full line advance in bake pixels

    for (int i = 0; i < kNumGlyphs; ++i) {
        const stbtt_packedchar& b = baked[i];
        Glyph& g = glyphs_[i];
        g.u0 = b.x0 / kAtlasW; g.v0 = b.y0 / kAtlasH;
        g.u1 = b.x1 / kAtlasW; g.v1 = b.y1 / kAtlasH;
        g.xoff = b.xoff; g.yoff = b.yoff;
        g.w = b.x1 - b.x0;
        g.h = b.y1 - b.y0;
        g.advance = b.xadvance;
        g.valid = true;
    }

    // Convert single-channel coverage -> RGBA (white, alpha = coverage) so the
    // shared sprite shader's `color * texture(...)` yields tinted text.
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kAtlasW) * kAtlasH * 4);
    for (int i = 0; i < kAtlasW * kAtlasH; ++i) {
        rgba[i * 4 + 0] = 255;
        rgba[i * 4 + 1] = 255;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = coverage[i];
    }

    Texture2DDesc desc;
    desc.width = kAtlasW;
    desc.height = kAtlasH;
    desc.channels = 4;
    desc.srgb = false;          // coverage is linear alpha, not colour
    desc.pixels = std::move(rgba);
    atlas_.Upload(desc);        // Stage B (render thread)

    loaded_ = atlas_.valid();
    if (!loaded_) return std::unexpected("Font: atlas upload failed");
    return {};
}

} // namespace gfx
