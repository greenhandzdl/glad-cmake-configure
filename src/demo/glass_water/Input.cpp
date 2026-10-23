// glass_water demo — fly-camera / switch implementation (see Input.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Input.h"

#include <algorithm>

#include <glm/glm.hpp>

namespace glass_water {

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
    // LeftShift is a brake rather than a boost: half of these objects are thin
    // sheets that a fast flight turns into a strobe, and the interesting part of
    // a transparency error is what it looks like at one metre's distance.
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
    // the only way back. Both go through ApplyCapture so the mouse reference point
    // is re-seeded (see there) whichever way the grab changes; Esc only ever
    // releases, because closing the window is X's job and a transparency demo
    // invites you to look at the frame rather than to quit it.
    if (in.Once(win.KeyIsDown(gldx::win::Key::F), Input::kCapture))
        ApplyCapture(win, in, in.captured);
    if (win.KeyIsDown(gldx::win::Key::Escape) && in.captured)
        ApplyCapture(win, in, true);
    if (win.KeyIsDown(gldx::win::Key::X)) win.Close();

    // Everything below moves the simulation or changes what the frame is made of,
    // so it belongs to the captured pointer. This is not a nicety: a --snapshot
    // run releases the cursor precisely so the desktop stays usable, the window
    // still takes keyboard focus while it is frontmost, and one stray keystroke
    // during the four seconds before the capture moved the camera far enough to
    // change 36 % of the pixels. A reproducibility claim that survives only when
    // nobody touches the keyboard is not a claim, and the same gate covers the
    // wheel (ScrollCallback) and the pointer (MouseCallback).
    if (!in.captured) return;

    // The four correctness switches, one key each. They are deliberately not
    // grouped: a single "naive transparency" key would hide which of the four
    // jobs is the one that was actually doing the work.
    if (in.Once(win.KeyIsDown(gldx::win::Key::Num1), Input::kSort))
        in.sortBackToFront = !in.sortBackToFront;
    if (in.Once(win.KeyIsDown(gldx::win::Key::Num2), Input::kDepthWrite))
        in.blendWritesDepth = !in.blendWritesDepth;
    if (in.Once(win.KeyIsDown(gldx::win::Key::Num3), Input::kShell))
        in.shellTwoPass = !in.shellTwoPass;
    if (in.Once(win.KeyIsDown(gldx::win::Key::Num4), Input::kMode))
        in.blend = static_cast<BlendMode>((static_cast<int>(in.blend) + 1) % 3);

    // Projection, on Tab like every other demo that has one. It is filed here
    // rather than under diagnostics because it is the one switch the fragment
    // stage has to be told about: an orthographic frustum has no eye position,
    // and the BRDF's viewing direction has to come from the camera forward
    // instead (Shaders.h). main.cpp re-seeds the framing on the flip so the
    // scene does not jump a scale factor on the way past.
    if (in.Once(win.KeyIsDown(gldx::win::Key::Tab), Input::kOrtho))
        in.ortho = !in.ortho;

    // Diagnostics.
    if (in.Once(win.KeyIsDown(gldx::win::Key::C), Input::kCull))
        in.cull = !in.cull;
    if (in.Once(win.KeyIsDown(gldx::win::Key::V), Input::kWire))
        in.wireframe = !in.wireframe;
    if (in.Once(win.KeyIsDown(gldx::win::Key::H), Input::kHud))
        in.hud = !in.hud;
    if (in.Once(win.KeyIsDown(gldx::win::Key::B), Input::kBloom))
        in.bloom = !in.bloom;

    // Held keys, scaled by dt so a slow machine sweeps at the same rate: sea
    // state and flight speed are the two dials it is worth dragging.
    constexpr float kRate = 0.6f;
    if (win.KeyIsDown(gldx::win::Key::RightBracket))
        in.wave = std::min(0.6f, in.wave + kRate * dt);
    if (win.KeyIsDown(gldx::win::Key::LeftBracket))
        in.wave = std::max(0.0f, in.wave - kRate * dt);
    if (win.KeyIsDown(gldx::win::Key::Equal))
        in.speed = std::min(40.0f, in.speed + 4.0f * dt);
    if (win.KeyIsDown(gldx::win::Key::Minus))
        in.speed = std::max(0.5f, in.speed - 4.0f * dt);
}

void ApplyCapture(gldx::win::Window& win, Input& in, bool keepCursor) {
    // `keepCursor` is the scripted-run override. With --snapshot / --freeze-at
    // the view has to stay exactly where --yaw/--pitch put it, and a captured
    // pointer hands the last word to whoever happens to move the mouse.
    in.captured = !keepCursor;
    win.SetCursorCaptured(in.captured);
    // Re-seed the delta reference. Taking the grab parks the pointer at the
    // window centre, and releasing it lets the OS cursor resume wherever it was;
    // either way the next cursor event would otherwise report the whole jump from
    // the last one and spin the camera a quarter turn on the first twitch.
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
    // stronger here than it looks: this callback is registered unconditionally, so
    // without it a scroll anywhere else on the desktop while the window held focus
    // re-framed a scripted capture. The fov is what the whole perspective rests on,
    // so the shot came back looking like a non-deterministic renderer.
    if (!in->captured) return;
    // Multiplicative rather than additive, so one notch zooms the same *fraction*
    // at every scale - the behaviour every viewport in existence has, and the one
    // that stops the first notch at fov 55 from being a lurch at fov 5.
    const float k = dy > 0.0 ? 0.9f : 1.1f;
    if (in->ortho) {
        // The ortho box is in world units, and the scene is 36 x 24 of basin, so
        // the clamp is the scene's own size rather than an arbitrary number: any
        // smaller and the panes leave the frame, any larger and the waves are two
        // pixels tall.
        in->orthoHeight = std::clamp(in->orthoHeight * k, 2.0f, 80.0f);
    } else {
        in->fov = std::clamp(in->fov * k, 8.0f, 110.0f);
    }
    // `dx` is a horizontal wheel or a two-finger swipe sideways. Zooming the view
    // box with it too would make a trackpad gesture spin the framing on an axis
    // the projection has no notion of, so it is dropped rather than guessed at.
    (void)dx;
}

} // namespace glass_water
