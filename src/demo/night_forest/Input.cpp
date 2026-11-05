// night_forest demo — fly-camera / switch implementation (see Input.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Input.h"

#include <algorithm>

#include <glm/glm.hpp>

namespace night_forest {

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};
const int kFontCandidateCount =
    static_cast<int>(sizeof(kFontCandidates) / sizeof(kFontCandidates[0]));

FlyIntent ReadFlyIntent(gldx::win::Window& win, const Input& in, float dt) {
    FlyIntent out;
    // See HandleKeys: a released pointer means the frame is nobody's to move.
    if (!in.captured) return out;
    // LeftShift is a brake rather than a boost. The interesting part of this scene
    // is a reflection a metre and a half across, and at 6 m/s a single frame steps
    // from one side of the crate to the other.
    const float step = in.speed * (win.KeyIsDown(gldx::win::Key::LeftShift) ? 0.35f : 1.0f) * dt;
    if (win.KeyIsDown(gldx::win::Key::W)) out.forward += step;
    if (win.KeyIsDown(gldx::win::Key::S)) out.forward -= step;
    if (win.KeyIsDown(gldx::win::Key::D)) out.strafe  += step;
    if (win.KeyIsDown(gldx::win::Key::A)) out.strafe  -= step;
    if (win.KeyIsDown(gldx::win::Key::Space)) out.rise += step;
    if (win.KeyIsDown(gldx::win::Key::LeftControl)) out.rise -= step;
    return out;
}

void HandleKeys(gldx::win::Window& win, Input& in, float dt) {
    // Pointer grab and quit stay live whichever way the cursor is held - they are
    // the only way back. Esc only ever releases, because closing the window is X's
    // job and a night scene invites you to look at the frame rather than quit it.
    if (in.Once(win.KeyIsDown(gldx::win::Key::F), Input::kGrab))
        ApplyCapture(win, in, in.captured);
    if (win.KeyIsDown(gldx::win::Key::Escape) && in.captured)
        ApplyCapture(win, in, true);
    if (win.KeyIsDown(gldx::win::Key::X)) win.Close();

    // Everything below changes what the frame is made of, so it belongs to the
    // captured pointer. This is not a nicety: a --snapshot run releases the cursor
    // precisely so the desktop stays usable, the window still takes keyboard focus
    // while it is frontmost, and one stray keystroke during the seconds before the
    // capture would move the camera or park the crate. A reproducibility claim that
    // survives only when nobody touches the keyboard is not a claim - and the same
    // gate covers the wheel (ScrollCallback) and the pointer (MouseCallback).
    if (!in.captured) return;

    // The five content switches, one key each, deliberately not grouped. Each is
    // independently responsible for something visible in the crate's faces, and a
    // single "naive night render" key would hide which job was being done.
    if (in.Once(win.KeyIsDown(gldx::win::Key::Num1), Input::kCapture))
        in.capture = !in.capture;
    if (in.Once(win.KeyIsDown(gldx::win::Key::Num2), Input::kRefract))
        in.refract = !in.refract;
    if (in.Once(win.KeyIsDown(gldx::win::Key::Num3), Input::kLights))
        in.lights = !in.lights;
    if (in.Once(win.KeyIsDown(gldx::win::Key::Num4), Input::kFog))
        in.fog = !in.fog;
    if (in.Once(win.KeyIsDown(gldx::win::Key::R), Input::kRoam))
        in.roam = !in.roam;

    // Diagnostics.
    if (in.Once(win.KeyIsDown(gldx::win::Key::C), Input::kCull))
        in.cull = !in.cull;
    if (in.Once(win.KeyIsDown(gldx::win::Key::V), Input::kWire))
        in.wireframe = !in.wireframe;
    if (in.Once(win.KeyIsDown(gldx::win::Key::H), Input::kHud))
        in.hud = !in.hud;
    if (in.Once(win.KeyIsDown(gldx::win::Key::B), Input::kBloom))
        in.bloom = !in.bloom;

    // Held keys, scaled by dt so a slow machine sweeps at the same rate.
    constexpr float kMixRate = 0.10f;
    if (win.KeyIsDown(gldx::win::Key::RightBracket))
        in.envMix = std::min(0.60f, in.envMix + kMixRate * dt);
    if (win.KeyIsDown(gldx::win::Key::LeftBracket))
        in.envMix = std::max(0.0f, in.envMix - kMixRate * dt);
    if (win.KeyIsDown(gldx::win::Key::Equal))
        in.speed = std::min(40.0f, in.speed + 4.0f * dt);
    if (win.KeyIsDown(gldx::win::Key::Minus))
        in.speed = std::max(0.5f, in.speed - 4.0f * dt);
    // No floor clamp: this is a flight camera, and the two ways to get under the
    // ground are to fly there or to spawn there with --pos-y, both of which are
    // answers the demo should be able to show rather than refuse.
}

void ApplyCapture(gldx::win::Window& win, Input& in, bool keepCursor) {
    // `keepCursor` is the scripted-run override. With --snapshot / --freeze-at the
    // view has to stay exactly where --yaw/--pitch put it, and a captured pointer
    // hands the last word to whoever happens to move the mouse.
    in.captured = !keepCursor;
    win.SetCursorCaptured(in.captured);
    // Re-seed the delta reference. Taking the grab parks the pointer at the window
    // centre, and releasing it lets the OS cursor resume wherever it was; either way
    // the next cursor event would otherwise report the whole jump from the last one
    // and spin the camera a quarter turn on the first twitch.
    const gldx::win::Vec2d cursor = win.CursorPos();
    in.lastX = cursor.x;
    in.lastY = cursor.y;
}

void MouseCallback(gldx::win::Window& win, gldx::win::Vec2d pos) {
    auto* in = static_cast<Input*>(win.GetUserData());
    if (!in) return;
    const float dx = static_cast<float>(pos.x - in->lastX);
    const float dy = static_cast<float>(pos.y - in->lastY);
    in->lastX = pos.x;
    in->lastY = pos.y;
    // Only a captured pointer drives the view. Released, the deltas are absolute
    // cursor jumps across the window and would spin the camera on any stray
    // movement; the caller who wants an unobstructed cursor also wants the view
    // left alone.
    if (!in->captured) return;
    // Right on the mouse turns right (a positive yaw turns left in the engine's
    // convention), and the pitch is clamped short of straight up / down so the
    // camera basis never degenerates.
    in->yaw   -= dx * 0.0032f;
    in->pitch  = std::clamp(in->pitch - dy * 0.0032f, -1.5f, 1.5f);
}

void ScrollCallback(gldx::win::Window& win, double dx, double dy) {
    auto* in = static_cast<Input*>(win.GetUserData());
    if (!in) return;
    // The same gate MouseCallback has, for the same reason - and the reason is
    // stronger here: this callback is registered unconditionally, so without it a
    // scroll anywhere else on the desktop while the window held focus re-framed a
    // scripted capture. The fov is what the whole perspective rests on, so the shot
    // came back looking like a non-deterministic renderer.
    if (!in->captured) return;
    // Multiplicative rather than additive, so one notch zooms the same *fraction*
    // at every scale.
    const float k = dy > 0.0 ? 0.9f : 1.1f;
    // 20 degrees is a telescope on the moon and 100 degrees puts the crate behind
    // the camera at the default spawn, so the range is the scene's own geometry.
    in->fov = std::clamp(in->fov * k, 20.0f, 100.0f);
    // `dx` is a horizontal wheel or a two-finger swipe sideways. Zooming with it too
    // would make a trackpad gesture spin the framing on an axis the projection has
    // no notion of, so it is dropped rather than guessed at.
    (void)dx;
}

} // namespace night_forest
