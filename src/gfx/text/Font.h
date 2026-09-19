#ifndef GFX_TEXT_FONT_H
#define GFX_TEXT_FONT_H

/**
 * @file Font.h
 * @brief stb_truetype-rasterised bitmap font for the HUD (plan "Font").
 *
 * Two-stage, matching the project threading model (plan section 1.2):
 *   Stage A (any thread, but here done inline on the render thread at load):
 *     read the .ttf/.otf/.ttc, bake the printable ASCII range into a tightly
 *     packed RGBA coverage atlas with stb_truetype's packer.
 *   Stage B (render thread): upload that atlas as a single Texture2D.
 *
 * Per-glyph data kept in bake-pixel units; TextRenderer multiplies by a scale
 * factor (desired pixel size / bake size) to draw at any size. `yoff`/`xoff`
 * follow the stb convention: offsets from the pen where the pen sits on the
 * baseline and +y is down, so glyph tops above the baseline have negative yoff.
 */

#include <cstdint>
#include <expected>
#include <string>

#include "gfx/texture/Texture2D.h"

namespace gfx {

class Font {
public:
    static constexpr int kFirstChar = 32;   // ' '
    static constexpr int kLastChar  = 126;  // '~'
    static constexpr int kNumGlyphs = kLastChar - kFirstChar + 1;

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
    Texture2D atlas_;
    Glyph     glyphs_[kNumGlyphs]{};
    float     bakeSize_ = 48.0f;
    float     ascent_ = 0;
    float     lineGap_ = 0;
    bool      loaded_ = false;
};

} // namespace gfx

#endif // GFX_TEXT_FONT_H
