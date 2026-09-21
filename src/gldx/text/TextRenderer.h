#ifndef GLDX_TEXT_TEXTRENDERER_H
#define GLDX_TEXT_TEXTRENDERER_H

/**
 * @file TextRenderer.h
 * @brief Draws HUD strings through a SpriteBatch using a baked Font atlas
 *        (plan "TextRenderer"). Stateless helper: all geometry goes through the
 *        shared batch, so many strings coalesce into few draw calls.
 *
 * Text is positioned from a top-left origin in pixels (matching SpriteBatch's
 * orthographic projection); '\n' advances to the next line by the font's line
 * height. Colour is a plain RGBA tint applied in the sprite shader.
 */

#include <cstddef>
#include <string>

#include <glm/glm.hpp>

#include "gldx/render/SpriteBatch.h"
#include "gldx/text/Font.h"

namespace gldx {

class TextRenderer {
public:
    static void Draw(SpriteBatch& batch, const Font& font, std::string_view text,
                     float x, float y, float pixelSize, const glm::vec4& color) {
        if (!font.loaded()) return;
        const float scale = pixelSize / font.bakeSize();
        const float lineH = font.lineGap() * scale;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            const std::size_t len = (nl == std::string_view::npos) ? text.size() - start
                                                                   : nl - start;
            DrawLine(batch, font, text.substr(start, len), x, y, scale, color);
            if (nl == std::string_view::npos) break;
            y += lineH;
            start = nl + 1;
        }
    }

    // Width in pixels of a single line (no newline). Used to size HUD panels.
    static float Measure(const Font& font, std::string_view text, float pixelSize) {
        if (!font.loaded()) return 0.0f;
        const float scale = pixelSize / font.bakeSize();
        float w = 0.0f;
        for (char c : text) {
            if (c == '\n') continue;
            if (const Font::Glyph* g = font.glyph(static_cast<unsigned char>(c)); g && g->valid)
                w += g->advance * scale;
        }
        return w;
    }

private:
    static void DrawLine(SpriteBatch& batch, const Font& font, std::string_view line,
                         float x, float topY, float scale, const glm::vec4& color) {
        const float baseline = topY + font.ascent() * scale;
        const Texture2D& atlas = font.atlas();
        float penX = x;
        for (char c : line) {
            const Font::Glyph* g = font.glyph(static_cast<unsigned char>(c));
            if (!g || !g->valid) continue;
            if (g->w > 0.0f && g->h > 0.0f) {
                const float gx = penX + g->xoff * scale;
                const float gy = baseline + g->yoff * scale;   // yoff is negative above baseline
                batch.Draw(atlas, gx, gy, g->w * scale, g->h * scale,
                           g->u0, g->v0, g->u1, g->v1, color);
            }
            penX += g->advance * scale;
        }
    }
};

} // namespace gldx

#endif // GLDX_TEXT_TEXTRENDERER_H
