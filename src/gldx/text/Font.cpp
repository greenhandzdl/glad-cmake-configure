module;

#include "gldx/gmf.hpp"

#include <cstring>
#include <fstream>

#ifdef GLDX_NO_FREETYPE
#include <stb_truetype.h>
#else
// ft2build.h is FreeType's documented entry point (it pins the ABI version and
// pulls ftheader.h); the face API is then spelled as a plain path rather than
// through the `#include FT_FREETYPE_H` macro idiom, because a macro-expanded
// include is one more thing the module dependency scanner has to resolve.
#include <ft2build.h>
#include <freetype/freetype.h>
#endif

module gldx;

namespace gldx {

namespace {

// The shortlist the HUD demos each carried a private copy of. A stock install
// has at least one of these, and the order prefers a monospace face because the
// panel is a column of numbers: proportional digits make a live counter jitter.
const char* const kSystemFonts[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

} // namespace

std::expected<void, std::string> Font::LoadFromFile(const std::string& path, float pixelSize) {
    RenderContext::AssertRenderThread("Font::LoadFromFile");

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::unexpected("Font: cannot open " + path);
    const std::streamoff size = file.tellg();
    // tellg() yields -1 for a non-seekable stream; a crafted or oversized file
    // must never drive an unbounded allocation (crash / DoS). Bound it first.
    constexpr std::streamoff kMaxFontBytes = 64LL * 1024 * 1024;   // 64 MB sanity limit
    if (size <= 0 || size > kMaxFontBytes)
        return std::unexpected("Font: invalid or oversized font file " + path);
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size))
        return std::unexpected("Font: cannot read " + path);

    // Stage A: whatever rasteriser this build was configured with fills a
    // single-channel coverage atlas and the glyph table that indexes it.
    std::vector<unsigned char> coverage(static_cast<std::size_t>(kAtlasW) * kAtlasH, 0);
    if (auto baked = Bake(data, pixelSize, coverage); !baked) return baked;

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

std::expected<void, std::string> Font::LoadSystem(float pixelSize) {
    if (loaded_) return {};
    std::string tried = "Font: none of the system fonts loaded:";
    for (const char* path : kSystemFonts) {
        if (LoadFromFile(path, pixelSize)) return {};
        tried += " ";
        tried += path;
    }
    return std::unexpected(tried);
}

#ifdef GLDX_NO_FREETYPE

const char* Font::backend() noexcept { return "stb_truetype"; }

std::expected<void, std::string> Font::Bake(const std::vector<unsigned char>& data,
                                            float pixelSize,
                                            std::vector<unsigned char>& coverage) {
    stbtt_fontinfo font;
    const int offset = stbtt_GetFontOffsetForIndex(data.data(), 0);
    if (!stbtt_InitFont(&font, data.data(), offset))
        return std::unexpected("Font: unrecognised font file");

    std::vector<stbtt_packedchar> baked(static_cast<std::size_t>(kNumGlyphs));
    std::memset(baked.data(), 0, sizeof(stbtt_packedchar) * static_cast<std::size_t>(kNumGlyphs));

    stbtt_pack_context pc;
    if (!stbtt_PackBegin(&pc, coverage.data(), kAtlasW, kAtlasH, 0, 1, nullptr))
        return std::unexpected("Font: atlas PackBegin failed");
    stbtt_PackSetOversampling(&pc, 2, 2);
    const int bakedSize = static_cast<int>(pixelSize);
    if (!stbtt_PackFontRange(&pc, data.data(), 0, static_cast<float>(bakedSize),
                             kFirstChar, kNumGlyphs, baked.data())) {
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
        const stbtt_packedchar& b = baked[static_cast<std::size_t>(i)];
        Glyph& g = glyphs_[i];
        // b.x0/y0/x1/y1 are unsigned-short bitmap pixels in this stb version; divide
        // in float or the result truncates to 0 and every glyph samples atlas (0,0).
        g.u0 = static_cast<float>(b.x0) / kAtlasW; g.v0 = static_cast<float>(b.y0) / kAtlasH;
        g.u1 = static_cast<float>(b.x1) / kAtlasW; g.v1 = static_cast<float>(b.y1) / kAtlasH;
        g.xoff = b.xoff; g.yoff = b.yoff;
        // Quad size must be the glyph's box in BAKE pixels, not the atlas
        // footprint. With PackSetOversampling(2,2) the atlas box (x1-x0) is
        // twice the bake-space box; using it stretched every glyph ~2x so
        // neighbours overlapped into an unreadable smear. xoff2-xoff is the
        // true bake-space extent (stb already divides it back out), while the
        // UV above still spans the full oversampled atlas cell.
        g.w = b.xoff2 - b.xoff;
        g.h = b.yoff2 - b.yoff;
        g.advance = b.xadvance;
        g.valid = true;
    }
    return {};
}

#else

const char* Font::backend() noexcept { return "freetype"; }

namespace {

// Transparent gutter around each atlas cell. The sprite sampler is GL_LINEAR
// with mipmaps, so a texel of ink right on a cell edge would mix the
// neighbour's coverage into it; two zero texels absorb the filter footprint at
// every mip level down to where the whole atlas starts blurring together.
constexpr int kCellPad = 2;

// RAII wrappers: every early return below has to give the library and the face
// back, and neither is recoverable by hand if one of two paths forgets it.
struct Library {
    FT_Library handle = nullptr;
    Library() = default;
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    ~Library() { if (handle) FT_Done_FreeType(handle); }
};

struct Face {
    FT_Face handle = nullptr;
    Face() = default;
    Face(const Face&) = delete;
    Face& operator=(const Face&) = delete;
    ~Face() { if (handle) FT_Done_Face(handle); }
};

// 26.6 fixed-point pixel -> float, the unit FreeType reports outlines in.
constexpr float ToPixels(FT_Pos v) { return static_cast<float>(v) / 64.0f; }

} // namespace

std::expected<void, std::string> Font::Bake(const std::vector<unsigned char>& data,
                                            float pixelSize,
                                            std::vector<unsigned char>& coverage) {
    Library lib;
    if (FT_Init_FreeType(&lib.handle))
        return std::unexpected("Font: FT_Init_FreeType failed");

    Face face;
    // The bytes stay owned by the caller's vector, which outlives this call -
    // FT_New_Memory_Face only borrows them, so there is nothing to free here
    // and no second copy of a font that can legitimately be tens of MB.
    if (FT_New_Memory_Face(lib.handle, data.data(), static_cast<FT_Long>(data.size()), 0,
                           &face.handle))
        return std::unexpected("Font: unrecognised font file");

    const int bakedSize = static_cast<int>(pixelSize);
    if (FT_Set_Pixel_Sizes(face.handle, 0, static_cast<FT_UInt>(bakedSize)))
        return std::unexpected("Font: FT_Set_Pixel_Sizes failed");
    bakeSize_ = static_cast<float>(bakedSize);

    // Same convention as the stb path: ascent and the full line advance in bake
    // pixels, so TextRenderer's baseline math and every HUD's line spacing are
    // unchanged by the switch. metrics.height is already ascent - descent +
    // line_gap, which is what stb's (a - d + lg) * scale computes.
    ascent_  = ToPixels(face.handle->size->metrics.ascender);
    lineGap_ = ToPixels(face.handle->size->metrics.height);

    // NO_HINTING on purpose. Full hinting runs the font's own bytecode or the
    // autohinter, both of which snap outlines to the pixel grid and can shift a
    // stem by a texel between platforms; the HUD is drawn scaled down from the
    // bake size anyway, so the grid alignment buys nothing that the filter
    // would not undo. Unhinted outlines rasterise bit-identically everywhere.
    constexpr FT_Int32 kLoadFlags = FT_LOAD_RENDER | FT_LOAD_NO_HINTING | FT_LOAD_TARGET_NORMAL;

    // Shelf packer, codepoints in order. Unlike stb's packer the cell size is
    // known before anything is placed, which is what makes the overflow check
    // below exact rather than "it failed somewhere in the range".
    int penX = kCellPad, penY = kCellPad, rowH = 0;
    for (int i = 0; i < kNumGlyphs; ++i) {
        Glyph& g = glyphs_[i];
        const int codepoint = kFirstChar + i;

        // Index 0 is .notdef. Every real UI face maps the printable ASCII range,
        // but a face that does not must yield a blank cell with a sane advance
        // rather than a box in the middle of the atlas.
        const FT_UInt index = FT_Get_Char_Index(face.handle, static_cast<FT_UInt32>(codepoint));
        if (FT_Load_Glyph(face.handle, index, kLoadFlags))
            return std::unexpected("Font: FT_Load_Glyph failed for codepoint " +
                                   std::to_string(codepoint));

        const FT_GlyphSlot slot = face.handle->glyph;
        const FT_Bitmap& bm = slot->bitmap;
        g.xoff = static_cast<float>(slot->bitmap_left);
        // FreeType measures the bitmap's top edge above the baseline, the atlas
        // convention measures the top-left corner below the pen with +y down.
        g.yoff = -static_cast<float>(slot->bitmap_top);
        g.advance = ToPixels(slot->advance.x);
        g.valid = true;

        const int w = static_cast<int>(bm.width);
        const int h = static_cast<int>(bm.rows);
        // Only the 8-bit grayscale mode this call asks for. Anything else (a
        // embedded bitmap strike, a monochrome-only face) becomes a blank cell:
        // wrong pixels would be invisible in the atlas but visible as garbage on
        // screen, whereas a blank is just a missing glyph.
        if (w <= 0 || h <= 0 || bm.pixel_mode != FT_PIXEL_MODE_GRAY) {
            g.w = 0; g.h = 0;
            g.u0 = g.v0 = g.u1 = g.v1 = 0;
            continue;
        }

        if (penX + w + kCellPad > kAtlasW) {   // next shelf
            penX = kCellPad;
            penY += rowH + kCellPad;
            rowH = 0;
        }
        if (penY + h + kCellPad > kAtlasH)
            return std::unexpected("Font: atlas overflow (glyphs do not fit)");

        for (int r = 0; r < h; ++r) {
            // bm.pitch can exceed width (row padding); copying width bytes out of
            // each row is what makes the gutter stay zero.
            std::memcpy(&coverage[static_cast<std::size_t>(penY + r) * kAtlasW + penX],
                        bm.buffer + static_cast<std::ptrdiff_t>(r) * bm.pitch,
                        static_cast<std::size_t>(w));
        }

        // UV spans the bitmap exactly; the gutter is only ever sampled, never
        // addressed, so a cell growing cannot steal texels from its neighbour.
        g.u0 = static_cast<float>(penX) / kAtlasW;
        g.v0 = static_cast<float>(penY) / kAtlasH;
        g.u1 = static_cast<float>(penX + w) / kAtlasW;
        g.v1 = static_cast<float>(penY + h) / kAtlasH;
        g.w = static_cast<float>(w);
        g.h = static_cast<float>(h);

        penX += w + kCellPad;
        rowH = rowH > h ? rowH : h;
    }
    return {};
}

#endif  // GLDX_NO_FREETYPE

} // namespace gldx
