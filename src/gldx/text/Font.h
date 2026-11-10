#ifndef GLDX_TEXT_FONT_H
#define GLDX_TEXT_FONT_H

/**
 * @file Font.h
 * @brief Bitmap font for the HUD: one baked coverage atlas + per-glyph metrics
 *        (plan "Font"). Rasterised by FreeType, with stb_truetype as the
 *        configure-time fallback.
 *
 * Two-stage, matching the project threading model (plan section 1.2):
 *   Stage A (any thread, but here done inline on the render thread at load):
 *     read the .ttf/.otf/.ttc, bake the printable ASCII range into a tightly
 *     packed RGBA coverage atlas.
 *   Stage B (render thread): upload that atlas as a single Texture2D.
 *
 * Who does Stage A is a configure-time choice, not an API one: FreeType when
 * the vendored submodule is enabled (the default, -DGLDX_ENABLE_FREETYPE=OFF
 * to drop it) and stb_truetype otherwise. Both fill the same glyph table, the
 * same atlas geometry and the same metric convention, so nothing above this
 * header can tell them apart; `backend()` reports which one for the HUD's own
 * evidence line, because "the text is drawn from a baked atlas" is true either
 * way and only that line says who baked it.
 *
 * Per-glyph data kept in bake-pixel units; TextRenderer multiplies by a scale
 * factor (desired pixel size / bake size) to draw at any size. `yoff`/`xoff`
 * follow the stb convention: offsets from the pen where the pen sits on the
 * baseline and +y is down, so glyph tops above the baseline have negative yoff.
 * The FreeType backend maps its own `bitmap_left` / `bitmap_top` onto exactly
 * that sign, which is why the layout code has no backend branch.
 */

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "gldx/texture/Texture2D.h"

namespace gldx {

class Font {
public:
    static constexpr int kFirstChar = 32;   // ' '
    static constexpr int kLastChar  = 126;  // '~'
    static constexpr int kNumGlyphs = kLastChar - kFirstChar + 1;

    // Atlas extent in texels. Sized so the whole printable range fits at the
    // default bake size with a gutter around every cell; a request that does
    // not fit is an error, never a silent truncation.
    static constexpr int kAtlasW = 1024;
    static constexpr int kAtlasH = 1024;

    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;   // atlas UV
        float xoff = 0, yoff = 0;               // top-left relative to pen (bake px)
        float w = 0, h = 0;                     // size in atlas (bake px)
        float advance = 0;                      // pen advance (bake px)
        bool  valid = false;
    };

    // Load + bake + upload. `pixelSize` is the size glyphs are baked at (higher
    // = crisper atlas, more memory). Returns false via std::expected on any I/O
    // or packing failure so callers can run with text disabled. Render-thread.
    std::expected<void, std::string> LoadFromFile(const std::string& path, float pixelSize = 48.0f);

    // LoadFromFile over the well-known system fonts, tried in order (the same
    // shortlist every HUD demo used to carry a private copy of). The error
    // string lists what was tried, so "no font on this box" stays diagnosable.
    // Render-thread, and a no-op success once an atlas is already loaded.
    std::expected<void, std::string> LoadSystem(float pixelSize = 48.0f);

    // Which rasteriser filled the atlas: "freetype" or "stb_truetype". Static
    // because it is fixed by the build, not by the file that was loaded.
    [[nodiscard]] static const char* backend() noexcept;

    [[nodiscard]] const Texture2D& atlas()  const noexcept { return atlas_; }
    [[nodiscard]] const Glyph*     glyph(int codepoint) const noexcept {
        if (codepoint < kFirstChar || codepoint > kLastChar) return nullptr;
        return &glyphs_[codepoint - kFirstChar];
    }
    [[nodiscard]] float bakeSize() const noexcept { return bakeSize_; }
    [[nodiscard]] float ascent()   const noexcept { return ascent_; }   // bake px
    [[nodiscard]] float lineGap()  const noexcept { return lineGap_; } // bake px
    [[nodiscard]] bool  loaded()   const noexcept { return loaded_; }

private:
    // Stage A, backend half. Rasterises the printable ASCII range into
    // `coverage` (kAtlasW x kAtlasH single-channel) and fills glyphs_ plus the
    // three metric fields. Font::backend() names whichever implementation is
    // compiled in; the two share this one declaration on purpose, so adding a
    // third rasteriser must not mean adding a third atlas format.
    std::expected<void, std::string> Bake(const std::vector<unsigned char>& file,
                                          float pixelSize,
                                          std::vector<unsigned char>& coverage);

    Texture2D atlas_;
    Glyph     glyphs_[kNumGlyphs]{};
    float     bakeSize_ = 48.0f;
    float     ascent_ = 0;
    float     lineGap_ = 0;
    bool      loaded_ = false;
};

} // namespace gldx

#endif // GLDX_TEXT_FONT_H
