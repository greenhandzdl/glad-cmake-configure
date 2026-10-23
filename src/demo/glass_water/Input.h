#ifndef GLASS_WATER_INPUT_H
#define GLASS_WATER_INPUT_H

/**
 * @file Input.h
 * @brief Free-flight camera state, the projection switch, and the four
 *        correctness switches that glass_water exists to demonstrate.
 *
 * Pulled out of main.cpp so the demo reads like a small project: main wires the
 * engine together and owns the camera, this file owns "what the user has asked
 * for". The camera pose lives here as position + yaw + pitch because the fly
 * camera is the position authority (nothing else moves it), and the mouse look
 * has to write to the same two angles the keyboard does.
 *
 * The four switches at the top of the struct are not display options: sorting,
 * depth writes, the two-pass shell and the blend equation are each independently
 * responsible for the transparent layer being right, which is why each gets its
 * own key and its own `--off` name. Switch any one of them off and the frame
 * loses one of the four guarantees - see BlendPass.cpp for what each one buys.
 *
 * The projection is a different kind of switch, and the file keeps its two
 * framings separate on purpose: `fov` and `orthoHeight` are the same act viewed
 * two ways ("how much of the world fits"), so the scroll wheel drives whichever
 * one is live rather than quietly re-using a value from the other mode. Only
 * uViewProj changes in the renderer; see Shaders.h for the one thing in the
 * fragment stage that genuinely has to know which projection is in use.
 *
 * NOTE: names gldx::win types (via `import gldxwin;`) and BlendMode from
 * Scene.h; Scene.h names gldx engine types, so include this AFTER `import gldx;`.
 */

import gldxwin;   // gldx::win::Window / Key / Vec2d - no GLFW here

#include <glm/glm.hpp>

#include "Scene.h"

namespace glass_water {

struct Input {
    // ---- free-flight camera pose -------------------------------------------
    // A positive pitch looks up in the engine's convention, so the spawn tilt is
    // negative: the basin and the panes, not the sky. The spawn point is chosen
    // so that the first frame the user sees is the demo's thesis - five panes of
    // different opacity with water, pillars and lamps behind every one of them -
    // rather than the outside of the near rim.
    glm::vec3 pos{0.0f, 2.6f, 15.0f};
    float yaw = 0.0f, pitch = -0.09f;
    float speed = 6.0f;
    double lastX = 0.0, lastY = 0.0;
    bool captured = true;

    // ---- the four correctness switches --------------------------------------
    bool sortBackToFront = true;   // 1: draw the blend layer far -> near
    bool blendWritesDepth = false; // 2: let the blend layer fill the depth buffer
    bool shellTwoPass = true;      // 3: closed shells drawn back face first
    // 4: cycles straight/premul/additive. Starts on premultiplied because that is
    // the equation a conservation-derived coverage is written against - the shader
    // scales its own radiance, so the blend unit must not. Straight does the same
    // multiply in hardware instead and lands on a pixel-identical frame; it stays
    // reachable as the control that proves the two are one contract, not a style.
    // Additive is the one that genuinely differs: nothing attenuates the
    // background, which is why it is also the control group for the sort switch.
    BlendMode blend = BlendMode::Premultiplied;

    // ---- projection ---------------------------------------------------------
    bool ortho = false;        // Tab: swap glm::perspective for glm::ortho
    float fov = 55.0f;         // live in perspective mode, wheel-adjusted
    float orthoHeight = 13.0f; // live in ortho mode, wheel-adjusted: the view box's
                               // vertical extent in world units at the target plane

    // ---- diagnostics --------------------------------------------------------
    bool cull = true;          // C: back-face culling for every layer
    bool wireframe = false;    // V: reveals which surface of each object is showing
    bool hud = true;           // H
    bool bloom = true;         // B: only the glow layer is bright enough to key on
    bool glow = true;          // whether the Glow layer is drawn at all
    float wave = 0.22f;        // [ ]: sea state, 0 = a flat, dead sheet

    // ---- key handling -------------------------------------------------------
    // Which switches are edge-triggered. The keys are polled once per frame (so
    // a press is visible no matter how short), hence "flip only on the frame the
    // key went down" and re-arm on release.
    enum Toggle : int {
        kSort, kDepthWrite, kShell, kMode, kOrtho, kCull, kWire, kHud, kBloom,
        kCapture, kToggleCount
    };
    bool armed[kToggleCount] = {true, true, true, true, true,
                                true, true, true, true, true};

    // True exactly once per press of the `slot` key.
    bool Once(bool down, int slot) {
        if (down && armed[slot]) { armed[slot] = false; return true; }
        if (!down) armed[slot] = true;
        return false;
    }
};

// A few well-known system fonts, tried in order; HUD text just no-ops if none
// are found, so a missing font never breaks the render.
extern const char* const kFontCandidates[];
extern const int kFontCandidateCount;

// Movement asked for by the keyboard, already scaled by speed and dt, expressed
// as three scalars in the camera's own frame rather than as a world vector: that
// way this header never has to name gldx::Camera, and main.cpp keeps the camera
// as the single authority on where "forward" is.
struct FlyIntent {
    float forward = 0.0f, strafe = 0.0f, rise = 0.0f;
};
FlyIntent ReadFlyIntent(gldx::win::Window& win, const Input& in, float dt);

// Every switch, plus the flight speed and the sea state. `dt` scales the two
// dials that are meant to be dragged (`[` `]` and `-` `=`) so they sweep at the
// same rate whatever the frame rate happens to be. F / Esc / X work with the
// pointer released - they are how it gets back - and nothing else does, so a
// scripted run cannot be steered by stray keystrokes (see HandleKeys).
void HandleKeys(gldx::win::Window& win, Input& in, float dt);

// The scroll wheel zooms whichever projection is live: field of view under
// perspective, view-box height under orthographic. Both are clamped to a range
// that keeps the near plane meaningful - an ortho box of zero height is a line,
// and a 3-degree fov makes the wave field unreadable. Like the pointer and the
// keys, the wheel is only listened to while the cursor is captured, so a scripted
// capture cannot be re-framed by whoever is scrolling the desktop meanwhile.
void ScrollCallback(gldx::win::Window& win, double dx, double dy);

// Captures / releases the pointer so the mouse can drive either the view
// (captured, relative deltas) or the cursor-position look (free, absolute).
void ApplyCapture(gldx::win::Window& win, Input& in, bool keepCursor = false);

void MouseCallback(gldx::win::Window& win, gldx::win::Vec2d pos);

} // namespace glass_water

#endif // GLASS_WATER_INPUT_H
