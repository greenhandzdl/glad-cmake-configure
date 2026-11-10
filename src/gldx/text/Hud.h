#ifndef GLDX_TEXT_HUD_H
#define GLDX_TEXT_HUD_H

/**
 * @file Hud.h
 * @brief The engine's instrument panel: coloured text lines on auto-sized
 *        translucent panels, anchored to the screen corners.
 *
 * Everything a demo HUD used to hand-roll lives here: the line list, the shared
 * foreground/dim/warning palette, measuring a panel to its widest line instead
 * of a magic width, the per-corner placement, the centre marker, and the
 * "sprite batch begin/end around the whole overlay" bookkeeping that makes a
 * panel one or two draw calls rather than one per glyph.
 *
 * A demo keeps exactly one job - deciding what its lines say. It either derives
 * HudPass and fills a Hud in Build(), or reaches for a Hud directly. Nothing
 * about the content is engine-side: the intake is strings, so the numbers a
 * panel reports can come from a pass's tallies, an input struct, or a
 * snprintf into a local buffer.
 *
 * Layout is expressed in framebuffer pixels from the top-left, matching
 * SpriteBatch and TextRenderer. Line advance comes from the font's own metrics
 * scaled to the section's pixel size, so switching the rasteriser behind Font
 * (FreeType or stb_truetype) moves the text with it instead of against it.
 */

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "gldx/render/RenderPass.h"

namespace gldx {

struct RenderFrame;

class Hud {
public:
    // Three roles with one meaning each, shared by every panel in the repo so
    // amber never means two different things on two different screens: kFg is
    // the claim the demo is making, kDim is the context around it, kWarn is
    // "this frame is missing something the panel asserts".
    static constexpr glm::vec4 kFg{1.0f, 1.0f, 1.0f, 0.92f};
    static constexpr glm::vec4 kDim{0.72f, 0.80f, 0.92f, 0.72f};
    static constexpr glm::vec4 kWarn{1.0f, 0.72f, 0.28f, 0.95f};

    // Translucent black behind a panel's lines; the near-invisible alpha is
    // what lets the scene read through the text.
    static constexpr glm::vec4 kPanel{0.0f, 0.0f, 0.0f, 0.55f};

    enum class Anchor { TopLeft, BottomLeft, TopRight, BottomRight };
    enum class Align  { Left, Right };
    // The screen-centre marker. Dot says "the pointer is captured" for a fly
    // camera; Plus is an aim point for a camera that can break blocks.
    enum class Marker { None, Dot, Plus };

    // Discards every section and line. Style, visibility and the marker are
    // kept, so this is what a per-frame rebuild starts with.
    void Clear() noexcept;

    // Starts a new section; following AddLine / AddBlock calls append to it.
    // Sections draw in the order they were opened, which only matters when two
    // of them share a corner.
    std::size_t BeginSection(Anchor anchor, float pixelSize,
                             const glm::vec4& panel = kPanel,
                             Align align = Align::Left,
                             float padding = 6.0f);

    // Appends one line to the open section. Text outside printable ASCII is
    // silently skipped by the atlas, so a typographic arrow shows up as
    // nothing at all rather than as an arrow - keep panel text in ASCII.
    void AddLine(std::string_view text, const glm::vec4& color = kFg);

    // Appends one line drawn at its own pixel size. The panel still sizes
    // itself to the widest line and its height becomes the sum of the per-line
    // advances, so a title and a body can share one box at different sizes -
    // which is how a demo shows off glyph scaling without inventing a second
    // panel for it.
    void AddLineAt(std::string_view text, float pixelSize, const glm::vec4& color = kFg);

    // Appends several lines that share one colour - the controls hint block.
    void AddLines(std::initializer_list<std::string_view> lines,
                  const glm::vec4& color = kDim);

    // Appends one already-newline-joined string as several lines.
    void AddBlock(std::string_view text, const glm::vec4& color = kFg);

    void SetVisible(bool on) noexcept { visible_ = on; }
    void SetMarker(Marker m) noexcept { marker_ = m; }
    [[nodiscard]] bool    visible()  const noexcept { return visible_; }
    [[nodiscard]] Marker  marker()   const noexcept { return marker_; }
    [[nodiscard]] std::size_t lineCount() const noexcept;

    // Draws every section (and the marker) through the frame's overlay
    // records. Does nothing when the panel is hidden, when the frame carries
    // no sprite batch / font / solid texel, or when the font never loaded -
    // a missing font disables text, it never stops a frame.
    void Draw(RenderFrame& f) const;

private:
    struct Line {
        std::string text;
        glm::vec4 color = kFg;
        // 0 means "use the section's pixelSize"; anything else is this line's
        // own size, and the row it occupies is advanced by that instead.
        float pixelSize = 0.0f;
    };

    struct Section {
        Anchor anchor = Anchor::TopLeft;
        Align  align  = Align::Left;
        float  pixelSize = 12.0f;
        float  padding = 6.0f;
        glm::vec4 panel = kPanel;
        std::vector<Line> lines;
    };

    std::vector<Section> sections_;
    std::size_t open_ = 0;      // index BeginSection last wrote
    bool visible_ = true;
    Marker marker_ = Marker::None;
};

// A render pass that owns a Hud: rebuild the lines, draw them, close the
// profiler frame. The profiler call happens on every exit path, including the
// hidden-panel one, because a HUD that returned early used to be the surest
// way to lose a frame's timing sample.
class HudPass : public RenderPass {
public:
    explicit HudPass(const char* name = "Hud") noexcept : RenderPass(name) {}

    void Execute(RenderFrame& frame) override;

    // The demo override: refill `hud` for this frame. Execute() has already
    // called Hud::Clear(), and Build() runs whether or not the panel is
    // visible - a demo that can skip its snprintfs should check
    // hud.visible() and return, which is what the instrument-panel demos do.
    virtual void Build(RenderFrame& frame);

    Hud hud;
};

} // namespace gldx

#endif // GLDX_TEXT_HUD_H
