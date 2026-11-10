/**
 * @file main.cpp
 * @brief text_hud - a 2D HUD: a baked bitmap font drawn through a sprite batch.
 *
 * Three small pieces cooperate. Font::LoadSystem finds a system TTF and bakes
 * printable ASCII into one tightly-packed RGBA coverage atlas (a single
 * Texture2D) at a chosen pixel size - with FreeType when the build has it and
 * with stb_truetype when it does not, which is a build choice, not an API one.
 * SpriteBatch is an immediate-mode quad batcher in top-left pixel coordinates
 * with its own orthographic projection. TextRenderer is stateless: for each
 * glyph it looks up its atlas rect + advance, scales by (desired size / bake
 * size) and pushes a tinted quad into the shared batch - so a whole HUD
 * collapses into a couple of draw calls, and multi-line strings just advance y
 * by the font's line gap.
 *
 * There is no 3D here on purpose: this is the text/overlay subsystem in
 * isolation, drawn over a flat clear. The bake size (48) is higher than any size
 * drawn, so down-scaled glyphs stay crisp. If no system font is found the demo
 * still runs (TextRenderer no-ops when the font is not loaded) - a missing font
 * is never fatal.
 *
 * The panel is drawn by gldx::Hud, so the only thing left in this file is the
 * three facts the demo exists to put on screen, and the one panel that shows
 * them at three different sizes.
 *
 * Controls: Esc quits, --quit-after SECONDS for headless, --snapshot PATH (with
 * --snap-at SECONDS, default 3, and --freeze-at SECONDS parking the clock) to
 * dump one frame and exit.
 *
 * The freeze is what makes two runs of one configuration comparable, with one
 * exception worth naming rather than hiding: the panel's first line prints the
 * live smoothed fps, and the panel IS the subject here, so there is no
 * `--off hud` to fall back on. A text_hud snapshot is evidence about layout
 * (and the standard pair of A/B frames for two font backends), not a
 * byte-identical pixel baseline.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#include <glm/glm.hpp>

namespace {

gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1;
    d.channels = 4;
    d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

// Owns the font atlas + sprite batch + a solid texel, and paints an HUD that
// reflows every frame (fps, elapsed, a wrapping paragraph, a right-aligned
// line). The layout is gldx::Hud's; what stays here is the demo's subject
// matter - load the font, hand the overlay records to the frame, say three
// things.
class TextHudPass final : public gldx::HudPass {
public:
    TextHudPass() : gldx::HudPass("Hud") {
        if (!sprite_.Init()) {
            std::fprintf(stderr, "SpriteBatch init failed\n");
            return;
        }
        // The candidate list lives behind Font now, because "which TTF on this
        // machine" is a platform fact rather than a demo's business.
        if (const auto r = font_.LoadSystem(48.0f); !r) {
            std::fprintf(stderr, "HUD font not found (%s); text disabled\n",
                         r.error().c_str());
        }
        white_.Upload(MakeSolidDesc());
    }

    void Execute(gldx::RenderFrame& f) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        // A soft diagonal-ish flat backdrop so the anti-aliased glyphs have contrast.
        glClearColor(0.10f, 0.12f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // gldx::Hud draws through the frame's overlay records, so this demo
        // hands its own over here instead of drawing around them. No profiler:
        // the panel makes no timing claim beyond the fps the app already smooths.
        f.overlay.sprite = &sprite_;
        f.overlay.font   = &font_;
        f.overlay.white  = &white_;
        gldx::HudPass::Execute(f);
    }

    void Build(gldx::RenderFrame& f) override {
        using Hud = gldx::Hud;
        char buf[128];

        // One panel, three sizes. The demo's thesis is that an 18 px glyph
        // comes out of a 48 px atlas, so the sizes have to sit next to each
        // other rather than be split across corners - hence AddLineAt.
        hud.BeginSection(Hud::Anchor::TopLeft, 20.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.4f),
                         Hud::Align::Left, 12.0f);
        hud.AddLineAt("gldx demo - text_hud", 26.0f, Hud::kFg);
        std::snprintf(buf, sizeof(buf), "%.0f fps   t=%.1fs   fb %dx%d",
                      f.smoothedFps, info_.time, f.fbWidth, f.fbHeight);
        hud.AddLineAt(buf, 20.0f, Hud::kDim);
        // One string carrying its own newlines, split by the Hud. The demo's
        // third claim is literally "multi-line via '\n'", so the line is handed
        // over the way the text pipeline takes it rather than as three separate
        // AddLine calls.
        hud.AddBlock(
            "Font bakes ASCII into one atlas; TextRenderer\n"
            "scales + tints each glyph through a SpriteBatch.\n"
            "Multi-line via '\\n'; colour is a plain RGBA tint.",
            Hud::kDim);
        // Which rasteriser filled the atlas is the one fact a screenshot cannot
        // show: FreeType and the stb_truetype fallback draw the same strings.
        std::snprintf(buf, sizeof(buf), "rasteriser %s   %.0f px bake   %d glyphs",
                      gldx::Font::backend(), font_.bakeSize(), gldx::Font::kNumGlyphs);
        hud.AddLineAt(buf, 16.0f, Hud::kDim);

        // A right-aligned, slowly cycling word - proves per-draw colour works.
        const int hue = static_cast<int>(info_.time * 2.0) % 8;
        static const char* const kColors[] = {"red", "orange", "yellow", "green",
                                              "cyan", "blue", "violet", "white"};
        hud.BeginSection(Hud::Anchor::TopRight, 22.0f, Hud::kPanel, Hud::Align::Right);
        hud.AddLine(kColors[hue],
                    glm::vec4(1.0f, 0.5f + 0.5f * std::sin(info_.time), 0.6f, 1.0f));

        // ---- controls, at the bottom ----------------------------------------
        hud.BeginSection(Hud::Anchor::BottomLeft, 12.0f,
                         glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));
        hud.AddLines({
            "Esc quit   --quit-after SECONDS headless",
            "--snapshot PATH one frame then quit   --help all flags",
            "--freeze-at SEC park the clock   --snap-at SEC when to dump",
        });
    }

    void setFrameInfo(gldx::win::FrameInfo info) noexcept { info_ = info; }

private:
    gldx::SpriteBatch sprite_;
    gldx::Font        font_;
    gldx::Texture2D   white_;
    gldx::win::FrameInfo info_;
};

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"snapshot", "snap-at", "freeze-at"}, "text_hud");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const std::string snapPath = flags.string("snapshot");
    const double snapAt        = flags.real("snap-at", 3.0, 0.1, 120.0);
    const double freezeAt      = flags.number("freeze-at");
    bool snapped = false;

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - text_hud (Font + SpriteBatch + TextRenderer)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();

    auto pass = std::make_unique<TextHudPass>();
    TextHudPass* raw = pass.get();
    renderer.AddPass(std::move(pass));

    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        // The pass reads elapsed time out of the frame info for both its `t=`
        // readout and the colour cycle, so parking the clock means handing it a
        // parked copy - same idiom as night_forest's animTime.
        gldx::win::FrameInfo fi = info;
        if (freezeAt > 0.0) fi.time = std::min(fi.time, freezeAt);
        raw->setFrameInfo(fi);

        renderer.Render(f);

        // Verification hook: dump the framebuffer once the panel has settled, so
        // a blank window can never masquerade as a clean exit. This is also how a
        // FreeType build and an stb_truetype build get diffed on the same strings.
        if (!snapPath.empty() && !snapped && info.time >= snapAt) {
            snapped = true;
            // Window-bound capture via the gldxwin member: the free
            // gldx::CaptureScreenshot lives in module-gldx purview and does not
            // resolve across the static-lib boundary on MSVC's module linker.
            if (info.window->CaptureScreenshot(snapPath))
                std::printf("snapshot: %s\n", snapPath.c_str());
            else
                std::fprintf(stderr, "snapshot failed: %s\n", snapPath.c_str());
            info.window->Close();
        }
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
