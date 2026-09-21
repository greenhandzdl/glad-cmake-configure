/**
 * @file main.cpp
 * @brief text_hud - a 2D HUD: a baked bitmap font drawn through a sprite batch.
 *
 * Three small pieces cooperate. Font::LoadFromFile reads a system TTF and, with
 * stb_truetype, bakes printable ASCII into one tightly-packed RGBA coverage
 * atlas (a single Texture2D) at a chosen pixel size. SpriteBatch is an
 * immediate-mode quad batcher in top-left pixel coordinates with its own
 * orthographic projection. TextRenderer is stateless: for each glyph it looks up
 * its atlas rect + advance, scales by (desired size / bake size) and pushes a
 * tinted quad into the shared batch - so a whole HUD collapses into a couple of
 * draw calls, and multi-line strings just advance y by the font's line gap.
 *
 * There is no 3D here on purpose: this is the text/overlay subsystem in
 * isolation, drawn over a flat clear. The bake size (48) is higher than any size
 * drawn, so down-scaled glyphs stay crisp. If no system font is found the demo
 * still runs (TextRenderer no-ops when the font is not loaded) - a missing font
 * is never fatal.
 *
 * Controls: Esc quits, --quit-after SECONDS for headless.
 */

#include "demo/demo_app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include <glm/glm.hpp>

namespace {

// A few well-known system fonts, tried in order (mirrors pbr_showcase).
const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};

gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1;
    d.channels = 4;
    d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

// Owns the font atlas + sprite batch + a solid texel, and paints an HUD that
// reflows every frame (fps, elapsed, a wrapping paragraph, a right-aligned line).
class HudPass : public gldx::RenderPass {
public:
    HudPass() : RenderPass("Hud") {
        if (!sprite_.Init()) {
            std::fprintf(stderr, "SpriteBatch init failed\n");
            return;
        }
        for (const char* candidate : kFontCandidates) {
            if (font_.LoadFromFile(candidate, 48.0f)) break;
        }
        if (!font_.loaded()) std::fprintf(stderr, "HUD font not found; text disabled\n");
        white_.Upload(MakeSolidDesc());
    }

    void Execute(gldx::RenderFrame& f) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        // A soft diagonal-ish flat backdrop so the anti-aliased glyphs have contrast.
        glClearColor(0.10f, 0.12f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!font_.loaded() || !white_.valid()) return;

        sprite_.Begin(white_, f.fbWidth, f.fbHeight);

        // Translucent panel sized to the widest line rather than a magic number.
        const char* title = "gldx demo - text_hud";
        char stats[96];
        std::snprintf(stats, sizeof(stats), "%.0f fps   t=%.1fs   fb %dx%d",
                      f.smoothedFps, info_.time, f.fbWidth, f.fbHeight);
        const float panelW = std::max(gldx::TextRenderer::Measure(font_, title, 26.0f),
                                      gldx::TextRenderer::Measure(font_, stats, 20.0f)) + 24.0f;
        sprite_.Draw(white_, 16.0f, 16.0f, panelW, 128.0f,
                     0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.4f));

        gldx::TextRenderer::Draw(sprite_, font_, title, 28.0f, 26.0f, 26.0f, glm::vec4(1.0f));
        gldx::TextRenderer::Draw(sprite_, font_, stats, 28.0f, 60.0f, 20.0f,
                                glm::vec4(0.75f, 0.85f, 1.0f, 1.0f));
        const char* body =
            "Font bakes ASCII into one atlas; TextRenderer\n"
            "scales + tints each glyph through a SpriteBatch.\n"
            "Multi-line via '\\n'; colour is a plain RGBA tint.";
        gldx::TextRenderer::Draw(sprite_, font_, body, 28.0f, 86.0f, 18.0f,
                                glm::vec4(0.85f, 0.85f, 0.88f, 1.0f));

        // A right-aligned, slowly cycling line - proves per-draw colour works.
        const int hue = static_cast<int>(info_.time * 2.0) % 8;
        const char* colors[] = {"red", "orange", "yellow", "green",
                                "cyan", "blue", "violet", "white"};
        const float sw = gldx::TextRenderer::Measure(font_, colors[hue], 22.0f);
        gldx::TextRenderer::Draw(sprite_, font_, colors[hue],
                                static_cast<float>(f.fbWidth) - sw - 24.0f, 24.0f, 22.0f,
                                glm::vec4(1.0f, 0.5f + 0.5f * std::sin(info_.time), 0.6f, 1.0f));
        sprite_.End();
    }

    void setFrameInfo(demo::FrameInfo info) noexcept { info_ = info; }

private:
    gldx::SpriteBatch sprite_;
    gldx::Font        font_;
    gldx::Texture2D   white_;
    demo::FrameInfo  info_;
};

} // namespace

int main(int argc, char** argv) {
    const demo::Flags flags(argc, argv, {}, {}, "text_hud");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    return demo::Run(flags, "gldx demo - text_hud (Font + SpriteBatch + TextRenderer)",
                     [&](demo::Ctx& ctx) -> int {
        auto pass = std::make_unique<HudPass>();
        HudPass* raw = pass.get();
        ctx.renderer.AddPass(std::move(pass));

        return ctx.Loop([&](gldx::RenderFrame& /*f*/, const demo::FrameInfo& info) {
            raw->setFrameInfo(info);   // pass reads elapsed time from the frame info
        });
    });
}
