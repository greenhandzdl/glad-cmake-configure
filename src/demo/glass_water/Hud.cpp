// glass_water demo — HUD render pass implementation (see Hud.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Hud.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace glass_water {
namespace {

// GLenum -> the name the equation is usually written with, so the overlay reads
// as "which blend is the driver actually running" instead of 0x0302. Only the
// factors this demo can ask for are listed; anything else prints as a number.
const char* BlendEnumName(int glEnum) {
    switch (glEnum) {
        case GL_ZERO:                return "ZERO";
        case GL_ONE:                 return "ONE";
        case GL_SRC_ALPHA:           return "SRC_ALPHA";
        case GL_ONE_MINUS_SRC_ALPHA: return "1-SRC_ALPHA";
        case GL_DST_ALPHA:           return "DST_ALPHA";
        case GL_ONE_MINUS_DST_ALPHA: return "1-DST_ALPHA";
        default:                     return nullptr;
    }
}

const char* YesNo(bool v) { return v ? "yes" : "no "; }

const char* ModeName(BlendMode m) {
    switch (m) {
        case BlendMode::Premultiplied: return "premultiplied";
        case BlendMode::Additive:      return "additive";
        case BlendMode::Straight:      break;
    }
    return "straight";
}

// One overlay line plus its colour: the state lines that describe a *broken*
// guarantee are drawn in amber, so the panel is scannable while flying rather
// than something to read carefully after the fact.
//
// Everything here is ASCII 32..126 on purpose - the baked font atlas covers
// exactly that range and TextRenderer silently skips anything outside it, so a
// typographic arrow would not render as an arrow but as a missing glyph.
constexpr glm::vec4 kFg{1.0f, 1.0f, 1.0f, 0.92f};
constexpr glm::vec4 kWarn{1.0f, 0.72f, 0.28f, 0.95f};
constexpr glm::vec4 kDim{0.72f, 0.80f, 0.92f, 0.72f};

struct Line {
    std::string text;
    glm::vec4 color = kFg;
};

} // namespace

void GlassHudPass::Execute(gldx::RenderFrame& f) {
    gldx::RenderContext::AssertRenderThread("GlassHudPass::Execute");
    if (!in || f.fbWidth <= 0) {
        if (f.overlay.profiler) f.overlay.profiler->EndFrame();
        return;
    }
    // H hides the panel, not the instrumenting: BlendPass still reads its state
    // back out of GL every frame, so a clean screenshot has the same numbers in
    // the process log it would have had on screen.
    if (!in->hud) {
        if (f.overlay.profiler) f.overlay.profiler->EndFrame();
        return;
    }

    std::vector<Line> lines;
    char buf[256];

    // Read before this pass calls EndFrame(), so the number on screen is the
    // previous frame's CPU span - the honest one, since the current frame is
    // still being drawn while the overlay is built.
    const float cpuMs = f.overlay.profiler ? f.overlay.profiler->CpuMs() : 0.0f;

    // ---- where the camera is, since "the object behind disappeared" is only
    // ---- observable from a specific distance to it ---------------------------
    std::snprintf(buf, sizeof(buf), "glass_water   fps %.1f   cpu %.2f ms", f.smoothedFps, cpuMs);
    lines.push_back({buf, kDim});
    std::snprintf(buf, sizeof(buf), "pos (%.1f, %.1f, %.1f)   speed %.1f   sea %.2f",
                  in->pos.x, in->pos.y, in->pos.z, in->speed, in->wave);
    lines.push_back({buf, kDim});

    // ---- which projection the vertices went through, and its framing ---------
    // Printed as the two numbers that actually define it, not just a mode name:
    // under perspective the framing is an angle, under orthographic it is a
    // world-space height, and the wheel moves whichever one is live. Reading the
    // pair is also how you confirm the Tab hand-over kept the scene the same size.
    if (in->ortho) {
        std::snprintf(buf, sizeof(buf), "proj ORTHOGRAPHIC  box height %.1f m", in->orthoHeight);
    } else {
        std::snprintf(buf, sizeof(buf), "proj PERSPECTIVE   fov %.1f deg", in->fov);
    }
    lines.push_back({buf, in->ortho ? kWarn : kDim});

    // ---- what the water is made of, read back out of the item table ----------
    // Not a caption the shader has to agree with: this is the same Surface the
    // pass uploaded as uniforms, so F0 printed here is the one the Fresnel derived
    // from the IOR. If a tweak to the material does not show up on this line, the
    // tweak never reached the GPU.
    if (const Scene* sc = blend ? blend->settings.scene : nullptr) {
        for (const Item& it : sc->items()) {
            if (std::string(it.label) != "water sheet") continue;
            const Surface& s = it.surf;
            const float f = (s.ior - 1.0f) / (s.ior + 1.0f);
            std::snprintf(buf, sizeof(buf),
                          "water  ior %.3f -> F0 %.2f%%  rough %.2f  path %.1f m",
                          s.ior, f * f * 100.0f, s.roughness, s.pathLen);
            lines.push_back({buf, kDim});
            // Which side of the (fragment, blend unit) contract pays the multiply by
            // coverage. Shader and GPU are the same arithmetic written in two
            // different places, so both are fine and the frame is identical; only
            // "nobody" is a real difference, because additive never attenuates the
            // background at all - hence amber there and not on `straight`.
            const char* weighted = in->blend == BlendMode::Premultiplied ? "shader"
                               : in->blend == BlendMode::Straight ? "GPU" : "nobody";
            std::snprintf(buf, sizeof(buf),
                          "absorb %.3f/%.3f/%.3f  a=1-(1-F)T  rgb*a by %s",
                          s.absorption.x, s.absorption.y, s.absorption.z, weighted);
            lines.push_back({buf, in->blend == BlendMode::Additive ? kWarn : kDim});
            break;
        }
    }

    // ---- the four guarantees, as the frame asked for them --------------------
    std::snprintf(buf, sizeof(buf), "[1] sort far->near  %s   [3] shell two-pass  %s",
                  YesNo(in->sortBackToFront), YesNo(in->shellTwoPass));
    lines.push_back({buf, in->sortBackToFront && in->shellTwoPass ? kFg : kWarn});
    std::snprintf(buf, sizeof(buf), "[2] blend depth    %s   [4] equation  %s",
                  in->blendWritesDepth ? "WRITE" : "no-write", ModeName(in->blend));
    lines.push_back({buf, in->blendWritesDepth ? kWarn : kFg});

    // ---- the same state as the driver holds it, read back inside the pass ----
    if (const BlendPass* b = blend) {
        const BlendPass::GLState gl = b->appliedState();
        char eq[96];
        const char* src = BlendEnumName(gl.srcRGB);
        const char* dst = BlendEnumName(gl.dstRGB);
        if (src && dst) {
            std::snprintf(eq, sizeof(eq), "%s / %s", src, dst);
        } else {
            std::snprintf(eq, sizeof(eq), "0x%04X / 0x%04X",
                          static_cast<unsigned>(gl.srcRGB & 0xFFFF),
                          static_cast<unsigned>(gl.dstRGB & 0xFFFF));
        }
        std::snprintf(buf, sizeof(buf), "GL  depthTest %s  depthWrite %s  blend %s  cull %s",
                      YesNo(gl.depthTest), YesNo(gl.depthWrite), YesNo(gl.blend), YesNo(gl.cull));
        // This is the line that proves the demo's thesis: switch [2] on and the
        // read-back flips to yes, which is exactly when pixels start getting eaten.
        lines.push_back({buf, gl.depthWrite ? kWarn : kFg});
        std::snprintf(buf, sizeof(buf), "GL  src/dst     %s", eq);
        lines.push_back({buf, kDim});
        std::snprintf(buf, sizeof(buf), "blend submissions %d   all layers %d   cpu %.2f ms",
                      b->blendDraws(), b->totalDraws(), cpuMs);
        lines.push_back({buf, kDim});

        // ---- the evidence that costs the most to fake: last frame's order ----
        // Printed from the sequence Execute() actually submitted in, so turning
        // sorting off rewrites this list instead of only changing a label above.
        const std::vector<BlendPass::Entry>& order = b->order();
        if (in->sortBackToFront) {
            if (order.empty()) {
                lines.push_back({"order: (blend layer empty)", kDim});
            } else {
                std::snprintf(buf, sizeof(buf), "drawn far->near (%zu submissions):", order.size());
                lines.push_back({buf, kDim});
                const std::size_t show = std::min<std::size_t>(order.size(), 7);
                for (std::size_t i = 0; i < show; ++i) {
                    const BlendPass::Entry& e = order[i];
                    if (e.tile >= 0) {
                        std::snprintf(buf, sizeof(buf), "  %5.1f m  %s tile %d",
                                      e.distance, e.label, e.tile);
                    } else {
                        std::snprintf(buf, sizeof(buf), "  %5.1f m  %s", e.distance, e.label);
                    }
                    lines.push_back({buf, kFg});
                }
                if (order.size() > show) {
                    std::snprintf(buf, sizeof(buf), "  ... %zu more, still far->near",
                                  order.size() - show);
                    lines.push_back({buf, kDim});
                }
            }
        } else {
            lines.push_back({"order: NOT SORTED - declaration order,", kWarn});
            lines.push_back({"       the sheet as one submission", kWarn});
        }
        if (in->blendWritesDepth) {
            lines.push_back({"> blend fills the depth buffer: anything", kWarn});
            lines.push_back({"  behind a pane is rejected by GL_LESS.", kWarn});
        }
    }

    // ---- controls, in their own panel so the evidence stays readable ---------
    std::vector<Line> keys = {
        {"WASD/Space/Ctrl fly  Shift brake  mouse look", kDim},
        {"1 sort  2 depth-write  3 shell  4 equation", kDim},
        {"Tab persp/ortho  wheel zoom  [ ] sea  - = speed", kDim},
        {"C cull  V wire  B bloom  H hud  F cursor  X quit", kDim},
    };

    if (!f.overlay.sprite || !f.overlay.font || !f.overlay.white ||
        !f.overlay.font->loaded()) {
        if (f.overlay.profiler) f.overlay.profiler->EndFrame();
        return;
    }

    const gldx::Font& font = *f.overlay.font;
    const float topPx = 13.0f;
    const float keyPx = 12.0f;
    const float advTop = font.lineGap() * (topPx / font.bakeSize());
    const float advKey = font.lineGap() * (keyPx / font.bakeSize());
    const float pad = 6.0f;

    auto widest = [&font](const std::vector<Line>& v, float px) {
        float w = 0.0f;
        for (const Line& l : v) w = std::max(w, gldx::TextRenderer::Measure(font, l.text, px));
        return w;
    };

    const float wTop = widest(lines, topPx);
    const float hTop = advTop * static_cast<float>(lines.size()) + pad * 2.0f;
    const float wKey = widest(keys, keyPx);
    const float hKey = advKey * static_cast<float>(keys.size()) + pad * 2.0f;
    const float keyY = static_cast<float>(f.fbHeight) - hKey - pad;

    f.overlay.sprite->Begin(*f.overlay.white, f.fbWidth, f.fbHeight);
    f.overlay.sprite->Draw(*f.overlay.white, pad, pad, wTop + pad * 2.0f, hTop,
                           0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
    f.overlay.sprite->Draw(*f.overlay.white, pad, keyY, wKey + pad * 2.0f, hKey,
                           0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));

    float y = pad + advTop * 0.25f;
    for (const Line& l : lines) {
        gldx::TextRenderer::Draw(*f.overlay.sprite, font, l.text, pad * 2.0f, y, topPx, l.color);
        y += advTop;
    }
    float ky = keyY + advKey * 0.25f;
    for (const Line& l : keys) {
        gldx::TextRenderer::Draw(*f.overlay.sprite, font, l.text, pad * 2.0f, ky, keyPx, l.color);
        ky += advKey;
    }

    // Not a crosshair: a fly camera has nothing under the centre of the screen
    // worth aiming at. The dot only says the pointer is captured, i.e. that the
    // mouse drives the view rather than the OS.
    if (in->captured) {
        const float cx = f.fbWidth * 0.5f, cy = f.fbHeight * 0.5f;
        f.overlay.sprite->Draw(*f.overlay.white, cx - 1.5f, cy - 1.5f, 3.0f, 3.0f,
                               0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.35f));
    }
    f.overlay.sprite->End();

    if (f.overlay.profiler) f.overlay.profiler->EndFrame();
}

} // namespace glass_water
