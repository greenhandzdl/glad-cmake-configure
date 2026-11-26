// The HUD panel: what the demo says about itself while it runs.
//
// Split out of main.cpp because it is presentation only - it reads Character and View,
// writes text into `hud`, and touches nothing else. Keeping it here also keeps the
// per-frame loop from being two-thirds printf.
//
// NOTE: names gldx types, so include AFTER `import gldx;`.
#pragma once

#include <cstdint>

#include "Character.h"
#include "Scene.h"
#include "View.h"

namespace anim {

// ---------------------------------------------------------------------------
// SkinPass: the floor and the character, drawn straight at the window (no
// PostProcessChain - this demo has no HDR/bloom to composite, and the shaders
// encode their own output).
// ---------------------------------------------------------------------------
class WalkHudPass : public gldx::HudPass {
public:
    WalkHudPass(const Character& character, const View& view)
        : HudPass("WalkHud"), character_(character), view_(view) {}

    void Build(gldx::RenderFrame& f) override {
        if (!hud.visible()) return;
        char line[192];

        hud.BeginSection(gldx::Hud::Anchor::TopLeft, 17.0f);
        hud.AddLine("anim-rigid - skinned FBX, procedural walk", gldx::Hud::kFg);
        const gldx::RiggedModel& model = character_.model;
        if (!model.ready()) {
            hud.AddLine(character_.error.empty() ? "loading rig (worker thread)..." : character_.error.c_str(),
                        character_.error.empty() ? gldx::Hud::kDim : gldx::Hud::kWarn);
        } else {
            std::snprintf(line, sizeof(line), "%d bones, %d meshes, %d verts, %d tris",
                          static_cast<int>(model.skeleton().size()),
                          static_cast<int>(model.meshes().size()),
                          static_cast<int>(model.vertexCount()),
                          static_cast<int>(model.triangleCount()));
            hud.AddLine(line, gldx::Hud::kDim);
            // The phase the *gait* is using, folded into 0..1 - the value the
            // pose is actually built from. (An earlier version printed
            // phaseFor(1.0f, ...), which silently reads 1.30x low at the default
            // speed; a number that disagrees with the picture is worse than none.)
            float cyclePhase = character_.phaseFor(view_.speed, static_cast<float>(view_.clock)) /
                               (2.0f * kPi);
            cyclePhase -= std::floor(cyclePhase);
            // The action name is the one thing the panel must say when it is not
            // walking: a frozen `--action crouch` frame and a frozen `--action run`
            // frame are both "a person standing there", and without this line the
            // screenshot cannot prove which one it is.
            const char* actionName = "walk";
            switch (view_.requestAction) {
                case Action::Idle:   actionName = "idle";   break;
                case Action::Walk:   actionName = "walk";   break;
                case Action::Run:    actionName = "run";    break;
                case Action::Crouch: actionName = "crouch"; break;
                case Action::CrouchWalk: actionName = "crouchwalk"; break;
                case Action::Attack: actionName = "attack"; break;
                case Action::Jump:   actionName = "jump";   break;
                case Action::Sit:    actionName = "sit";    break;
                case Action::Swim:   actionName = "swim";   break;
            }
            std::snprintf(line, sizeof(line), "%s %s | gait phase %.2f cycle | %.2f m/s",
                          actionName, view_.walk ? "on " : "off",
                          static_cast<double>(cyclePhase),
                          static_cast<double>(view_.speed));
            hud.AddLine(line, gldx::Hud::kFg);
            std::snprintf(line, sizeof(line), "height %.2f m (rig unit x%.3g) | leg %.2f m | cycle %.2f m",
                          static_cast<double>(model.heightMetres()),
                          static_cast<double>(model.unitScale()),
                          static_cast<double>(model.legLengthMetres()),
                          static_cast<double>(character_.cycleMetres));
            hud.AddLine(line, gldx::Hud::kDim);
            std::snprintf(line, sizeof(line), "palette %d/%d bones | bind check %.1e | %d draws",
                          static_cast<int>(character_.palette.size()), gldx::kMaxBonePalette,
                          static_cast<double>(model.bindDeviation()), view_.drawCalls);
            hud.AddLine(line, gldx::Hud::kDim);
            // The two numbers that decide whether a frame reads as "standing" or
            // "in the air": the vertical shift applied to the whole character, and
            // where its lowest sole ended up. `air` names the part of the shift that
            // came from a jump arc rather than from the ground solve, so a jump whose
            // arc is not reaching the picture is caught by reading one line.
            if (view_.airRise > 0.0f) {
                std::snprintf(line, sizeof(line),
                              "ground: shift %+6.1f mm (air %+6.0f mm) | lowest sole %+6.1f mm",
                              static_cast<double>(view_.groundLift * 1000.0f),
                              static_cast<double>(view_.airRise * 1000.0f),
                              static_cast<double>(view_.soleHeight * 1000.0f));
            } else {
                std::snprintf(line, sizeof(line), "ground: shift %+6.1f mm | lowest sole %+6.1f mm",
                              static_cast<double>(view_.groundLift * 1000.0f),
                              static_cast<double>(view_.soleHeight * 1000.0f));
            }
            hud.AddLine(line, gldx::Hud::kDim);
            // The maps come out of the FBX itself, so this line is the claim
            // "textured", not "a texture file was found next to the model".
            std::snprintf(line, sizeof(line), "maps: albedo %s normal %s metal %s rough %s (%dx%d max)",
                          model.hasAlbedo() ? "y" : "n", model.hasNormal() ? "y" : "n",
                          model.hasMetallic() ? "y" : "n", model.hasRoughness() ? "y" : "n",
                          model.textureWidth(), model.textureHeight());
            hud.AddLine(line, gldx::Hud::kDim);
        }
        std::snprintf(line, sizeof(line), "t %.2f s%s | %.0f fps", view_.clock,
                      view_.freezeAt > 0.0f ? " (frozen)" : "", f.smoothedFps);
        hud.AddLine(line, view_.freezeAt > 0.0f ? gldx::Hud::kWarn : gldx::Hud::kDim);

        hud.BeginSection(gldx::Hud::Anchor::BottomLeft, 14.0f);
        hud.AddLines({
            "0 idle  1 walk  2 run  4 crouchwalk  5 attack  6 jump  7 sit  8 swim",
            "W walk on/off   G floor   F wireframe   H hud",
            "drag orbit   wheel zoom   Esc quit",
        }, gldx::Hud::kDim);
    }

private:
    const Character& character_;
    const View&      view_;
};

gldx::Texture2DDesc MakeSolidDesc(std::uint8_t r = 255, std::uint8_t g = 255,
                                  std::uint8_t b = 255, std::uint8_t a = 255,
                                  bool srgb = false) {
    gldx::Texture2DDesc d;
    d.width = 1;
    d.height = 1;
    d.channels = 4;
    d.srgb = srgb;
    d.pixels = {r, g, b, a};
    return d;
}

// Stage B for the character: everything here needs the GL context, which is why
// it runs on the render thread inside the frame callback once the worker's
// future is ready. The upload itself is one call now - RiggedModel derives the
// unit scale, the orientation, the rest-world cache and the palette names, and
// owns the 1x1 stand-in maps. What is left is this demo's own bookkeeping.

}  // namespace anim
