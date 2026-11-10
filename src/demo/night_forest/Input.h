#ifndef NIGHT_FOREST_INPUT_H
#define NIGHT_FOREST_INPUT_H

/**
 * @file Input.h
 * @brief The night_forest flight camera and the switches that decide what the
 *        frame is made of.
 *
 * Split out of main.cpp so the demo reads like a small project: main wires the
 * engine together and owns the camera, this file owns "what the user has asked
 * for". The pose lives here as position + yaw + pitch because the fly camera is
 * the position authority - nothing else moves it - and mouse look has to write to
 * the same two angles the keyboard does.
 *
 * The switches at the top are grouped by what they can prove, not by how many
 * keys are left over:
 *
 *   1 capture  - replaces the crate's environment cube with the FBO one. This is
 *                the demo's headline: off, the box mirrors the sky; on, it mirrors
 *                the forest, the lanterns and the ground behind you.
 *   2 refract  - removes the refracted ray and leaves the reflection. Both come out
 *                of the same captured cube, so this isolates the one line in the
 *                fragment stage that decides whether the box shows what is through
 *                it or what is around it.
 *   3 lights   - the four lanterns as point lights (their emissive glass stays).
 *                Off, the scene has only the moon, and the warm pool that the Gooch
 *                band is built around disappears.
 *   4 fog      - night mist. Off, the tree line stops at a hard silhouette instead
 *                of dissolving into the shadow tone.
 *   R roam     - parks or releases the crate. With it parked, no face of the capture
 *                cube changes, so the HUD's faces=0 line and a completely static
 *                reflection are the same event.
 *   G walk     - hands the camera to the walking path. It is not a camera speed
 *                change: the pose stops being an integral of `dt` and becomes a
 *                lookup in a seeded table whose Y comes from the height field, so
 *                the view climbs a hill in the screenshot and in the next run of
 *                the same command at the same second. Flying is what makes the
 *                relief inspectable; walking is what makes it a place.
 *
 * NOTE: names only gldx::win types (Window / Key / Vec2d), never an engine type,
 * which is why this header can be included before or after `import gldx;`.
 */

import gldxwin;   // gldx::win::Window / Key / Vec2d - no GLFW here

#include <glm/glm.hpp>

namespace night_forest {

struct Input {
    // ---- the five content switches -----------------------------------------
    bool capture = true;    // 1: render the crate's cube from an FBO
    bool refract = true;    // 2: bend the ray as well as mirror it
    bool lights  = true;    // 3: the lanterns light the scene, not just themselves
    bool fog     = true;    // 4: distance dissolves into the shadow tone
    bool roam    = true;    // R: the crate wanders; parked = the cube stops changing
    bool walk    = false;   // G: follow the walking path over the relief (needs a terrain)

    // ---- diagnostics --------------------------------------------------------
    bool cull      = true;  // C: back-face culling, the one switch that can make the
                            //    faceted canopy lose half of every tier
    bool wireframe = false; // V: shows what the instance count is actually paying for
    bool hud       = true;  // H
    bool bloom     = true;  // B: the only thing above 1.0 in the scene is the lantern glass

    float envMix = 0.18f;   // [ ]: how much of the wood is spent on the cube

    // ---- free-flight camera pose -------------------------------------------
    // Spawned 11 m up and 34 m out along +Z, looking down by 15 degrees. The height
    // is the load-bearing part: the tallest tree reaches 2.3 * 4 m, so a camera at
    // eye level is necessarily *inside* the band it is meant to be showing - the
    // trunk of one pine fills a third of the frame and the clearing, the crate and
    // the four lanterns are behind it. Above the canopy the same frame holds all of
    // them, and the 34 m stand-off keeps the crate inside the mist's start at 45.
    // A positive pitch looks up in the engine's convention, hence the negative.
    // While `walk` is on, `pos` and `yaw` are written by the path every frame and
    // only `pitch` is still the user's - see the switch list above.
    glm::vec3 pos{0.0f, 11.0f, 34.0f};
    float yaw = 0.0f, pitch = -0.26f;
    float speed = 6.0f;     // - =
    float fov   = 58.0f;    // wheel
    double lastX = 0.0, lastY = 0.0;
    bool captured = true;

    // ---- key handling -------------------------------------------------------
    // Which switches are edge-triggered. Keys are polled once per frame (so a
    // press is visible however short), hence "flip only on the frame the key went
    // down" and re-arm on release.
    enum Toggle : int {
        kCapture, kRefract, kLights, kFog, kRoam,
        kCull, kWire, kHud, kBloom, kWalk, kGrab, kToggleCount
    };
    bool armed[kToggleCount] = {true, true, true, true, true,
                                true, true, true, true, true, true};

    // True exactly once per press of the `slot` key.
    bool Once(bool down, int slot) {
        if (down && armed[slot]) { armed[slot] = false; return true; }
        if (!down) armed[slot] = true;
        return false;
    }
};

// Movement asked for by the keyboard, already scaled by speed and dt, expressed as
// three scalars in the camera's own frame rather than as a world vector: that way
// this header never has to name gldx::Camera, and main.cpp keeps the camera as the
// single authority on where "forward" is.
struct FlyIntent {
    float forward = 0.0f, strafe = 0.0f, rise = 0.0f;
};
FlyIntent ReadFlyIntent(gldx::win::Window& win, const Input& in, float dt);

// Every switch, plus the flight speed and the environment mix. `dt` scales the two
// dials meant to be dragged so they sweep at the same rate whatever the frame rate
// happens to be. F / Esc / X work with the pointer released - they are how it gets
// back - and nothing else does, so a scripted run cannot be steered by stray
// keystrokes (see the gate inside).
void HandleKeys(gldx::win::Window& win, Input& in, float dt);

// The scroll wheel narrows the field of view, clamped to a range that keeps the
// moon disc recognisable. Like the pointer and the keys, it is only listened to
// while the cursor is captured.
void ScrollCallback(gldx::win::Window& win, double dx, double dy);

// Captures / releases the pointer so the mouse can drive either the view
// (captured, relative deltas) or the OS cursor (free).
void ApplyCapture(gldx::win::Window& win, Input& in, bool keepCursor = false);

void MouseCallback(gldx::win::Window& win, gldx::win::Vec2d pos);

} // namespace night_forest

#endif // NIGHT_FOREST_INPUT_H
