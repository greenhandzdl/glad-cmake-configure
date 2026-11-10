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
// The three panel roles come from the engine now, so amber means the same
// thing here as it does in night_forest: "this frame is missing something the
// panel is claiming". They are spelled out as `Hud::kFg` rather than aliased
// into a local `constexpr glm::vec4 kFg = Hud::kFg;`: clang does not care about
// that alias, MSVC 14.44 does - ODR-using it sends its object writer into the
// 4 GB limit (C1605). The same failing prefix emits a 6.7 KB object when the
// imported constant is named directly and no object at all when the alias is:
// run 42781895430. The palette cannot drift from the engine's either, because
// this file holds no copy of it to drift.
//
// Everything here is ASCII 32..126 on purpose - the baked font atlas covers
// exactly that range and TextRenderer silently skips anything outside it, so a
// typographic arrow would not render as an arrow but as a missing glyph.
using Hud = gldx::Hud;

} // namespace

void GlassHudPass::Build(gldx::RenderFrame& f) {
    if (!in) return;
    // H hides the panel, not the instrumenting: BlendPass still reads its state
    // back out of GL every frame, so a clean screenshot has the same numbers in
    // the process log it would have had on screen. Only the string building is
    // skipped, because that is the panel's own cost.
    hud.SetVisible(in->hud);
    if (!in->hud) return;

    // The evidence block: one section, top-left, sized by the engine to the
    // widest line rather than by a number this file used to keep in its head.
    hud.BeginSection(Hud::Anchor::TopLeft, 13.0f);
    char buf[256];

    // Read before this pass calls EndFrame(), so the number on screen is the
    // previous frame's CPU span - the honest one, since the current frame is
    // still being drawn while the overlay is built.
    const float cpuMs = f.overlay.profiler ? f.overlay.profiler->CpuMs() : 0.0f;

    // ---- where the camera is, since "the object behind disappeared" is only
    // ---- observable from a specific distance to it ---------------------------
    std::snprintf(buf, sizeof(buf), "glass_water   fps %.1f   cpu %.2f ms", f.smoothedFps, cpuMs);
    hud.AddLine(buf, Hud::kDim);
    std::snprintf(buf, sizeof(buf), "pos (%.1f, %.1f, %.1f)   speed %.1f   sea %.2f",
                  in->pos.x, in->pos.y, in->pos.z, in->speed, in->wave);
    hud.AddLine(buf, Hud::kDim);

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
    hud.AddLine(buf, in->ortho ? Hud::kWarn : Hud::kDim);

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
            hud.AddLine(buf, Hud::kDim);
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
            hud.AddLine(buf, in->blend == BlendMode::Additive ? Hud::kWarn : Hud::kDim);
            break;
        }
    }

    // ---- the four guarantees, as the frame asked for them --------------------
    std::snprintf(buf, sizeof(buf), "[1] sort far->near  %s   [3] shell two-pass  %s",
                  YesNo(in->sortBackToFront), YesNo(in->shellTwoPass));
    hud.AddLine(buf, in->sortBackToFront && in->shellTwoPass ? Hud::kFg : Hud::kWarn);
    std::snprintf(buf, sizeof(buf), "[2] blend depth    %s   [4] equation  %s",
                  in->blendWritesDepth ? "WRITE" : "no-write", ModeName(in->blend));
    hud.AddLine(buf, in->blendWritesDepth ? Hud::kWarn : Hud::kFg);

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
        hud.AddLine(buf, gl.depthWrite ? Hud::kWarn : Hud::kFg);
        std::snprintf(buf, sizeof(buf), "GL  src/dst     %s", eq);
        hud.AddLine(buf, Hud::kDim);
        std::snprintf(buf, sizeof(buf), "blend submissions %d   all layers %d   cpu %.2f ms",
                      b->blendDraws(), b->totalDraws(), cpuMs);
        hud.AddLine(buf, Hud::kDim);

        // ---- the evidence that costs the most to fake: last frame's order ----
        // Printed from the sequence Execute() actually submitted in, so turning
        // sorting off rewrites this list instead of only changing a label above.
        const std::vector<BlendPass::Entry>& order = b->order();
        if (in->sortBackToFront) {
            if (order.empty()) {
                hud.AddLine("order: (blend layer empty)", Hud::kDim);
            } else {
                std::snprintf(buf, sizeof(buf), "drawn far->near (%zu submissions):", order.size());
                hud.AddLine(buf, Hud::kDim);
                const std::size_t show = std::min<std::size_t>(order.size(), 7);
                for (std::size_t i = 0; i < show; ++i) {
                    const BlendPass::Entry& e = order[i];
                    if (e.tile >= 0) {
                        std::snprintf(buf, sizeof(buf), "  %5.1f m  %s tile %d",
                                      e.distance, e.label, e.tile);
                    } else {
                        std::snprintf(buf, sizeof(buf), "  %5.1f m  %s", e.distance, e.label);
                    }
                    hud.AddLine(buf, Hud::kFg);
                }
                if (order.size() > show) {
                    std::snprintf(buf, sizeof(buf), "  ... %zu more, still far->near",
                                  order.size() - show);
                    hud.AddLine(buf, Hud::kDim);
                }
            }
        } else {
            hud.AddLine("order: NOT SORTED - declaration order,", Hud::kWarn);
            hud.AddLine("       the sheet as one submission", Hud::kWarn);
        }
        if (in->blendWritesDepth) {
            hud.AddLine("> blend fills the depth buffer: anything", Hud::kWarn);
            hud.AddLine("  behind a pane is rejected by GL_LESS.", Hud::kWarn);
        }
    }

    // ---- controls, in their own panel so the evidence stays readable ---------
    hud.BeginSection(Hud::Anchor::BottomLeft, 12.0f,
                     glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));
    hud.AddLines({
        "WASD/Space/Ctrl fly  Shift brake  mouse look",
        "1 sort  2 depth-write  3 shell  4 equation",
        "Tab persp/ortho  wheel zoom  [ ] sea  - = speed",
        "C cull  V wire  B bloom  H hud  F cursor  X quit",
    });

    // Not a crosshair: a fly camera has nothing under the centre of the screen
    // worth aiming at. The dot only says the pointer is captured, i.e. that the
    // mouse drives the view rather than the OS. HudPass::Execute draws it, and
    // closes the profiler frame, on every path out of here.
    hud.SetMarker(in->captured ? Hud::Marker::Dot : Hud::Marker::None);
}

} // namespace glass_water
