// Engine HUD overlay (see Hud.h). Implementation unit of module gldx: the
// SpriteBatch / Font / TextRenderer / RenderFrame declarations come from the
// implicitly imported primary interface, so this file adds no gldx includes.
module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

namespace {

// Where the first line sits inside a panel, as a fraction of one line advance.
// Without it the text baseline touches the panel's top edge and the ascenders
// get clipped by the eye rather than by the quad.
constexpr float kTextTopInset = 0.25f;

// The centre marker's two shapes, kept at the sizes the demos drew by hand:
// a fly camera has nothing under the centre worth aiming at, so its dot only
// reports that the pointer is captured; a camera that can break blocks needs
// a real aim point and gets the heavier plus.
constexpr float kDotHalf = 1.5f;
constexpr float kPlusHalf = 8.0f;
constexpr float kPlusThick = 1.0f;

} // namespace

void Hud::Clear() noexcept {
    sections_.clear();
    open_ = 0;
}

std::size_t Hud::BeginSection(Anchor anchor, float pixelSize, const glm::vec4& panel,
                              Align align, float padding) {
    Section s;
    s.anchor = anchor;
    s.pixelSize = pixelSize;
    s.panel = panel;
    s.align = align;
    s.padding = padding;
    sections_.push_back(std::move(s));
    open_ = sections_.size() - 1;
    return open_;
}

void Hud::AddLine(std::string_view text, const glm::vec4& color) {
    AddLineAt(text, 0.0f, color);
}

void Hud::AddLineAt(std::string_view text, float pixelSize, const glm::vec4& color) {
    // A panel that was never opened gets a default top-left one rather than
    // dropping the line: a demo that only says one thing should not have to
    // describe a layout to say it.
    if (sections_.empty()) BeginSection(Anchor::TopLeft, 12.0f);
    sections_[open_].lines.push_back({std::string(text), color, pixelSize});
}

void Hud::AddLines(std::initializer_list<std::string_view> lines, const glm::vec4& color) {
    for (std::string_view l : lines) AddLine(l, color);
}

void Hud::AddBlock(std::string_view text, const glm::vec4& color) {
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::size_t len = (nl == std::string_view::npos) ? text.size() - start
                                                               : nl - start;
        AddLine(text.substr(start, len), color);
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
}

std::size_t Hud::lineCount() const noexcept {
    std::size_t n = 0;
    for (const Section& s : sections_) n += s.lines.size();
    return n;
}

void Hud::Draw(RenderFrame& f) const {
    if (!visible_ || sections_.empty() || f.fbWidth <= 0 || f.fbHeight <= 0) return;
    if (!f.overlay.sprite || !f.overlay.font || !f.overlay.white ||
        !f.overlay.font->loaded()) {
        return;
    }

    const Font& font = *f.overlay.font;
    SpriteBatch& batch = *f.overlay.sprite;
    const Texture2D& white = *f.overlay.white;

    batch.Begin(white, f.fbWidth, f.fbHeight);
    for (const Section& s : sections_) {
        if (s.lines.empty()) continue;   // an opened-but-never-filled panel is not a box

        // A line's own size wins over the section's, so one panel can hold a
        // 26 px title above an 18 px body without becoming two panels.
        auto linePx = [&s](const Line& l) {
            return l.pixelSize > 0.0f ? l.pixelSize : s.pixelSize;
        };
        // Line advance comes from the font's own metrics at the size drawn,
        // so panels track the typeface instead of a constant that silently
        // clips the moment a font's ascent and height disagree with it.
        auto lineAdv = [&font, &linePx](const Line& l) {
            return font.lineGap() * (linePx(l) / font.bakeSize());
        };

        float widest = 0.0f;
        float bodyH = 0.0f;
        for (const Line& l : s.lines) {
            widest = std::max(widest, TextRenderer::Measure(font, l.text, linePx(l)));
            bodyH += lineAdv(l);
        }

        const float pad = s.padding;
        const float panelW = widest + pad * 2.0f;
        const float panelH = bodyH + pad * 2.0f;

        float x = pad;
        float y = pad;
        const float fw = static_cast<float>(f.fbWidth);
        const float fh = static_cast<float>(f.fbHeight);
        if (s.anchor == Anchor::TopRight || s.anchor == Anchor::BottomRight)
            x = fw - panelW - pad;
        if (s.anchor == Anchor::BottomLeft || s.anchor == Anchor::BottomRight)
            y = fh - panelH - pad;
        // A panel wider or taller than the framebuffer would go negative here.
        // Clamped rather than trusted: degenerate viewports happen (the
        // multi-window demos open small ones on purpose) and text pushed off
        // the left edge reads as "the HUD is gone" instead of as itself.
        x = std::max(x, pad);
        y = std::max(y, pad);

        if (s.panel.a > 0.0f)
            batch.Draw(white, x, y, panelW, panelH, 0.0f, 0.0f, 1.0f, 1.0f, s.panel);

        float ty = y + lineAdv(s.lines.front()) * kTextTopInset;
        for (const Line& l : s.lines) {
            const float px = linePx(l);
            float tx = x + pad;
            if (s.align == Align::Right)
                tx = x + panelW - pad - TextRenderer::Measure(font, l.text, px);
            TextRenderer::Draw(batch, font, l.text, tx, ty, px, l.color);
            ty += lineAdv(l);
        }
    }

    if (marker_ != Marker::None) {
        const float cx = static_cast<float>(f.fbWidth) * 0.5f;
        const float cy = static_cast<float>(f.fbHeight) * 0.5f;
        if (marker_ == Marker::Dot) {
            batch.Draw(white, cx - kDotHalf, cy - kDotHalf, kDotHalf * 2.0f, kDotHalf * 2.0f,
                       0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.35f));
        } else {
            batch.Draw(white, cx - kPlusHalf, cy - kPlusThick, kPlusHalf * 2.0f, kPlusThick * 2.0f,
                       0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.75f));
            batch.Draw(white, cx - kPlusThick, cy - kPlusHalf, kPlusThick * 2.0f, kPlusHalf * 2.0f,
                       0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.75f));
        }
    }
    batch.End();
}

void HudPass::Execute(RenderFrame& frame) {
    RenderContext::AssertRenderThread("HudPass::Execute");
    hud.Clear();
    Build(frame);
    hud.Draw(frame);
    // The frame closes whether or not anything was drawn: the profiler spans
    // the whole frame, and a HUD that skipped EndFrame while hidden would make
    // "H" look like a timing change rather than a display one.
    if (frame.overlay.profiler) frame.overlay.profiler->EndFrame();
}

void HudPass::Build(RenderFrame& /*frame*/) {}

} // namespace gldx
