// night_forest demo — HUD render pass implementation (see Hud.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace night_forest {
namespace {

// One overlay line plus its colour. Amber is reserved for "this frame is missing
// something the demo is claiming": the whole point of the panel is that you can
// tell at a glance whether the crate is really mirroring a forest or only a sky.
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

const char* YesNo(bool v) { return v ? "on " : "off"; }

} // namespace

void NightHudPass::Execute(gldx::RenderFrame& f) {
    gldx::RenderContext::AssertRenderThread("NightHudPass::Execute");
    if (!in || !scene || f.fbWidth <= 0) {
        if (f.overlay.profiler) f.overlay.profiler->EndFrame();
        return;
    }
    // H hides the panel, not the instrumenting: NightPass still tallies its draws
    // and reads back which cube was bound every frame, so a clean screenshot has the
    // same numbers in the process log it would have had on screen.
    if (!in->hud) {
        if (f.overlay.profiler) f.overlay.profiler->EndFrame();
        return;
    }

    std::vector<Line> lines;
    char buf[256];

    // Read before this pass calls EndFrame(), so the number on screen is the
    // previous frame's CPU span - the honest one, since the current frame is still
    // being drawn while the overlay is built.
    const float cpuMs = f.overlay.profiler ? f.overlay.profiler->CpuMs() : 0.0f;

    std::snprintf(buf, sizeof(buf), "night_forest   fps %.1f   cpu %.2f ms",
                  f.smoothedFps, cpuMs);
    lines.push_back({buf, kDim});
    std::snprintf(buf, sizeof(buf), "pos (%.1f, %.1f, %.1f)  fov %.0f  speed %.1f",
                  in->pos.x, in->pos.y, in->pos.z, in->fov, in->speed);
    lines.push_back({buf, kDim});

    // ---- the claim, in two numbers ------------------------------------------
    // Counted by the pass as it submitted, from the InstancedMesh objects it drew,
    // so it is the scene's own arithmetic: five instanced objects, one instance
    // each for the ground / crate, ~700 for each species. The capture's own
    // submissions are on the next line rather than folded in here, because adding
    // six times the draw count to the figure would argue against the sentence.
    if (pass) {
        std::snprintf(buf, sizeof(buf),
                      "world %d draws / %u instances on the last frame",
                      pass->drawsLastFrame(), pass->instancesLastFrame());
        lines.push_back({buf, kFg});
        std::snprintf(buf, sizeof(buf),
                      "capture %d draws for %d faces   (6 views of the same world)",
                      pass->captureDrawsLastFrame(), pass->facesCaptured());
        lines.push_back({buf, kDim});
    }
    std::snprintf(buf, sizeof(buf), "planted %u fir + %u broadleaf, %u rejected  seed %u",
                  scene->plantedFir(), scene->plantedBroad(), scene->rejected(),
                  scene->config.seed);
    // Amber when the field could not hold what was asked for: the scatter gives up
    // after eight attempts per tree, so a dense ring silently plants fewer.
    lines.push_back({buf,
        scene->plantedFir() + scene->plantedBroad()
            < static_cast<std::uint32_t>(scene->config.treeCount) ? kWarn : kDim});

    // ---- what the crate is sampling -----------------------------------------
    // Not the `capture` flag: the texture unit that was bound. With the flag on and
    // the FBO incomplete the pass falls back here, and a panel that printed the
    // intention would be the last place the truth showed up.
    if (pass) {
        const bool live = pass->captureLive();
        std::snprintf(buf, sizeof(buf), "env %s  %d px  mix %.2f  lod %.1f",
                      pass->envSource(), pass->envSizeLive(), in->envMix, envLod);
        lines.push_back({buf, live ? kFg : kWarn});
        if (!in->capture) {
            lines.push_back({"> capture OFF: the box mirrors the sky, not the", kWarn});
            lines.push_back({"  forest behind it. 1 brings the FBO cube back.", kWarn});
        } else if (live && pass->facesCaptured() == 0) {
            std::snprintf(buf, sizeof(buf), "   faces 0 = crate parked, cube reused");
            lines.push_back({buf, kDim});
        }
    }
    std::snprintf(buf, sizeof(buf), "crate  ior %.2f  refract %s   at (%.1f, %.1f, %.1f)",
                  ior, YesNo(in->refract),
                  scene->cratePose().pos.x, scene->cratePose().pos.y,
                  scene->cratePose().pos.z);
    lines.push_back({buf, in->refract ? kDim : kWarn});

    // Where the wander has got to. The heading is the table's own yaw, so watching
    // it change is watching the seeded path, and the distance is the camera's to the
    // crate - the number that decides whether the reflection is legible at all.
    {
        const double local = animTime - scene->config.wander.startAt;
        const bool moving = in->roam && local > 0.0;
        const glm::vec3 d = scene->cratePose().pos - in->pos;
        std::snprintf(buf, sizeof(buf),
                      "wander %s  t %.1f s  heading %.0f deg  %.1f m away",
                      moving ? "moving" : "parked", animTime,
                      scene->cratePose().yaw * 57.29577951308232f,
                      std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
        lines.push_back({buf, moving ? kDim : kWarn});
    }

    // ---- the light model ----------------------------------------------------
    // The pair the band is made of, printed as the numbers the fragment stage got.
    // A shadow that reads as black would be a shadowLift of zero, and this is the
    // only place that would show up before the eye had to guess.
    if (pass) {
        const NightPass::Settings& s = pass->settings;
        std::snprintf(buf, sizeof(buf), "gooch band %.2f  warm(%.2f,%.2f,%.2f)",
                      s.bandWidth, s.lightTint.x, s.lightTint.y, s.lightTint.z);
        lines.push_back({buf, kDim});
        std::snprintf(buf, sizeof(buf), "  cold(%.2f,%.2f,%.2f) lift(%.3f,%.3f,%.3f)",
                      s.shadowTint.x, s.shadowTint.y, s.shadowTint.z,
                      s.shadowLift.x, s.shadowLift.y, s.shadowLift.z);
        lines.push_back({buf, kDim});
    }
    if (field) {
        std::snprintf(buf, sizeof(buf), "moon dir (%.2f, %.2f, %.2f)  %.1f x",
                      field->moonDir.x, field->moonDir.y, field->moonDir.z,
                      field->moonIntensity);
        lines.push_back({buf, kDim});
    }
    if (!skySource.empty()) {
        lines.push_back({skySource, kDim});
    }
    std::snprintf(buf, sizeof(buf), "lanterns %s (%d points)   fog %s %.0f-%.0f m",
                  YesNo(in->lights), static_cast<int>(scene->lanterns().size()),
                  YesNo(in->fog), fogStart, fogEnd);
    lines.push_back({buf, (in->lights && in->fog) ? kDim : kWarn});
    if (frozenAt > 0.0) {
        std::snprintf(buf, sizeof(buf), "clock frozen at %.2f s", frozenAt);
        lines.push_back({buf, kWarn});
    }

    // ---- controls, in their own panel so the evidence stays readable ---------
    const std::vector<Line> keys = {
        {"WASD/Space/Ctrl fly  Shift brake  mouse look", kDim},
        {"1 capture  2 refract  3 lanterns  4 fog  R roam", kDim},
        {"C cull  V wire  B bloom  H hud  F cursor  wheel fov", kDim},
        {"[ ] env mix   - = speed   X quit  Esc release", kDim},
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

    // Not a crosshair: a fly camera has nothing under the centre of the screen worth
    // aiming at. The dot only says the pointer is captured, i.e. that the mouse
    // drives the view rather than the OS.
    if (in->captured) {
        const float cx = f.fbWidth * 0.5f, cy = f.fbHeight * 0.5f;
        f.overlay.sprite->Draw(*f.overlay.white, cx - 1.5f, cy - 1.5f, 3.0f, 3.0f,
                               0.0f, 0.0f, 1.0f, 1.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.35f));
    }
    f.overlay.sprite->End();

    if (f.overlay.profiler) f.overlay.profiler->EndFrame();
}

} // namespace night_forest
