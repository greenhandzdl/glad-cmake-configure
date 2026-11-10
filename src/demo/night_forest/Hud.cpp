// night_forest demo — HUD render pass implementation (see Hud.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include <glm/glm.hpp>

namespace night_forest {
namespace {

// The three panel roles come from the engine now, so amber means the same
// thing here as it does in glass_water: "this frame is missing something the
// panel is claiming". Aliased rather than spelled out because the line list
// below is long, and because a demo palette that could drift from the engine's
// is exactly how two panels end up meaning different things by the same colour.
//
// Everything here is ASCII 32..126 on purpose - the baked font atlas covers
// exactly that range and TextRenderer silently skips anything outside it, so a
// typographic arrow would not render as an arrow but as a missing glyph.
using Hud = gldx::Hud;
constexpr glm::vec4 kFg   = Hud::kFg;
constexpr glm::vec4 kDim  = Hud::kDim;
constexpr glm::vec4 kWarn = Hud::kWarn;

const char* YesNo(bool v) { return v ? "on " : "off"; }

} // namespace

void NightHudPass::Build(gldx::RenderFrame& f) {
    if (!in || !scene) return;
    // H hides the panel, not the instrumenting: NightPass still tallies its draws
    // and reads back which cube was bound every frame, so a clean screenshot has the
    // same numbers in the process log it would have had on screen. Only the string
    // building is skipped, because that is the panel's own cost.
    hud.SetVisible(in->hud);
    if (!in->hud) return;

    // The evidence block: one section, top-left, sized by the engine to the
    // widest line rather than by a number this file used to keep in its head.
    hud.BeginSection(Hud::Anchor::TopLeft, 13.0f);
    char buf[256];

    // Read before this pass calls EndFrame(), so the number on screen is the
    // previous frame's CPU span - the honest one, since the current frame is still
    // being drawn while the overlay is built.
    const float cpuMs = f.overlay.profiler ? f.overlay.profiler->CpuMs() : 0.0f;

    std::snprintf(buf, sizeof(buf), "night_forest   fps %.1f   cpu %.2f ms",
                  f.smoothedFps, cpuMs);
    hud.AddLine(buf, kDim);
    std::snprintf(buf, sizeof(buf), "pos (%.1f, %.1f, %.1f)  fov %.0f  speed %.1f",
                  in->pos.x, in->pos.y, in->pos.z, in->fov, in->speed);
    hud.AddLine(buf, kDim);

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
        hud.AddLine(buf, kFg);
        std::snprintf(buf, sizeof(buf),
                      "capture %d draws for %d faces   (6 views of the same world)",
                      pass->captureDrawsLastFrame(), pass->facesCaptured());
        hud.AddLine(buf, kDim);
    }
    std::snprintf(buf, sizeof(buf), "planted %u fir + %u broadleaf, %u rejected  seed %u",
                  scene->plantedFir(), scene->plantedBroad(), scene->rejected(),
                  scene->config.seed);
    // Amber when the field could not hold what was asked for: the scatter gives up
    // after eight attempts per tree, so a dense ring silently plants fewer.
    hud.AddLine(buf,
        scene->plantedFir() + scene->plantedBroad()
            < static_cast<std::uint32_t>(scene->config.treeCount) ? kWarn : kDim);

    // ---- the ground ---------------------------------------------------------
    // The flat sheet gets a line of its own rather than silence, because a panel
    // that only speaks when there are hills cannot tell a mistyped --terrain from a
    // terrain that loaded. The second line is the mesh the manual's 3.1 double loop
    // actually produced, and the third is what the camera is standing on - the
    // reading that makes a "walking over a ridge" screenshot checkable in numbers.
    {
        const Terrain& t = scene->terrain();
        if (t.reliefActive()) {
            std::snprintf(buf, sizeof(buf), "terrain %s  %u x %u cells at %.2f m  relief %.1f m",
                          TerrainModeName(t.mode()), t.cells(), t.cells(), t.cell(), t.relief());
            hud.AddLine(buf, kFg);
            std::snprintf(buf, sizeof(buf), "  %llu verts / %llu indices  %+.1f..%+.1f m"
                          "  smooth %d  levels %d",
                          static_cast<unsigned long long>(t.vertexCount()),
                          static_cast<unsigned long long>(t.indexCount()),
                          t.minHeight(), t.maxHeight(), t.smoothing(), t.levels());
            hud.AddLine(buf, kDim);
            std::snprintf(buf, sizeof(buf), "  %s", t.source().c_str());
            hud.AddLine(buf, kDim);
            // Gradient of the height field under the eye, sampled two cells apart so
            // it is the slope the shading uses rather than one vertex's noise spike.
            const float span = std::max(t.cell(), 0.5f);
            const float gx = scene->heightAt(in->pos.x + span, in->pos.z)
                           - scene->heightAt(in->pos.x - span, in->pos.z);
            const float gz = scene->heightAt(in->pos.x, in->pos.z + span)
                           - scene->heightAt(in->pos.x, in->pos.z - span);
            const float grad = std::sqrt(gx * gx + gz * gz) / (2.0f * span);
            std::snprintf(buf, sizeof(buf), "  under foot %+.2f m  slope %.0f deg",
                          scene->heightAt(in->pos.x, in->pos.z),
                          std::atan(grad) * 57.29577951308232f);
            hud.AddLine(buf, kDim);
        } else {
            std::snprintf(buf, sizeof(buf), "ground flat sheet  %.0f m half-width  %d uv tiles",
                          scene->groundHalf(), scene->grassTile());
            hud.AddLine(buf, kDim);
        }
    }
    if (in->walk) {
        std::snprintf(buf, sizeof(buf), "walk path  t %.1f s  eye %.2f m  heading %.0f deg",
                      animTime, scene->config.walkEye, in->yaw * 57.29577951308232f);
        hud.AddLine(buf, kFg);
    }

    // ---- what the crate is sampling -----------------------------------------
    // Not the `capture` flag: the texture unit that was bound. With the flag on and
    // the FBO incomplete the pass falls back here, and a panel that printed the
    // intention would be the last place the truth showed up.
    if (pass) {
        const bool live = pass->captureLive();
        std::snprintf(buf, sizeof(buf), "env %s  %d px  mix %.2f  lod %.1f",
                      pass->envSource(), pass->envSizeLive(), in->envMix, envLod);
        hud.AddLine(buf, live ? kFg : kWarn);
        if (!in->capture) {
            hud.AddLine("> capture OFF: the box mirrors the sky, not the", kWarn);
            hud.AddLine("  forest behind it. 1 brings the FBO cube back.", kWarn);
        } else if (live && pass->facesCaptured() == 0) {
            std::snprintf(buf, sizeof(buf), "   faces 0 = crate parked, cube reused");
            hud.AddLine(buf, kDim);
        }
    }
    std::snprintf(buf, sizeof(buf), "crate  ior %.2f  refract %s   at (%.1f, %.1f, %.1f)",
                  ior, YesNo(in->refract),
                  scene->cratePose().pos.x, scene->cratePose().pos.y,
                  scene->cratePose().pos.z);
    hud.AddLine(buf, in->refract ? kDim : kWarn);

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
        hud.AddLine(buf, moving ? kDim : kWarn);
    }

    // ---- the light model ----------------------------------------------------
    // The pair the band is made of, printed as the numbers the fragment stage got.
    // A shadow that reads as black would be a shadowLift of zero, and this is the
    // only place that would show up before the eye had to guess.
    if (pass) {
        const NightPass::Settings& s = pass->settings;
        std::snprintf(buf, sizeof(buf), "gooch band %.2f  warm(%.2f,%.2f,%.2f)",
                      s.bandWidth, s.lightTint.x, s.lightTint.y, s.lightTint.z);
        hud.AddLine(buf, kDim);
        std::snprintf(buf, sizeof(buf), "  cold(%.2f,%.2f,%.2f) lift(%.3f,%.3f,%.3f)",
                      s.shadowTint.x, s.shadowTint.y, s.shadowTint.z,
                      s.shadowLift.x, s.shadowLift.y, s.shadowLift.z);
        hud.AddLine(buf, kDim);
    }
    if (field) {
        std::snprintf(buf, sizeof(buf), "moon dir (%.2f, %.2f, %.2f)  %.1f x",
                      field->moonDir.x, field->moonDir.y, field->moonDir.z,
                      field->moonIntensity);
        hud.AddLine(buf, kDim);
    }
    if (!skySource.empty()) {
        hud.AddLine(skySource, kDim);
    }
    std::snprintf(buf, sizeof(buf), "lanterns %s (%d points)   fog %s %.0f-%.0f m",
                  YesNo(in->lights), static_cast<int>(scene->lanterns().size()),
                  YesNo(in->fog), fogStart, fogEnd);
    hud.AddLine(buf, (in->lights && in->fog) ? kDim : kWarn);
    if (frozenAt > 0.0) {
        std::snprintf(buf, sizeof(buf), "clock frozen at %.2f s", frozenAt);
        hud.AddLine(buf, kWarn);
    }

    // Who drew the glyphs is a build fact, and the panel is the only place it
    // can show up: FreeType and the stb_truetype fallback print the same
    // strings, so "the text is baked from a system TTF" is true either way and
    // only this line says which rasteriser filled the atlas this run.
    if (f.overlay.font) {
        std::snprintf(buf, sizeof(buf), "font %s  %.0f px bake  %d glyphs  %dx%d atlas",
                      gldx::Font::backend(), f.overlay.font->bakeSize(),
                      gldx::Font::kNumGlyphs, gldx::Font::kAtlasW, gldx::Font::kAtlasH);
        hud.AddLine(buf, kDim);
    }

    // ---- controls, in their own panel so the evidence stays readable ---------
    hud.BeginSection(Hud::Anchor::BottomLeft, 12.0f,
                     glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));
    hud.AddLines({
        "WASD/Space/Ctrl fly  Shift brake  mouse look",
        "1 capture  2 refract  3 lanterns  4 fog  R roam",
        "G walk the path  C cull  V wire  B bloom  H hud",
        "F cursor  wheel fov  [ ] env mix  - = speed",
        "X quit  Esc release",
    });

    // Not a crosshair: a fly camera has nothing under the centre of the screen
    // worth aiming at. The dot only says the pointer is captured, i.e. that the
    // mouse drives the view rather than the OS. HudPass::Execute draws it, and
    // closes the profiler frame, on every path out of here.
    hud.SetMarker(in->captured ? Hud::Marker::Dot : Hud::Marker::None);
}

} // namespace night_forest
