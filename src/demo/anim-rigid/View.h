// The demo's view/input state: camera orbit, framing, and what the keys asked for.
//
// Split out of main.cpp because it is pure state plus one bookkeeping helper - it
// touches neither the rig, the render passes, nor GL. Everything that needs it only
// needs the type.
#pragma once

// NOTE: names gldx engine types (gldx::OrbitCamera), so include AFTER `import gldx;`.

namespace anim {

// Which action is being asked for. A free enum in the demo namespace rather than a
// member of Character: the input callback records it into the View, the frame loop
// turns it into a machine Request. Keeping it out of Character is what lets the
// callback stay free of the animation machine.
enum class Action { Idle, Walk, Run, Crouch, CrouchWalk, Attack, Jump, Sit, Swim };

struct View {
    // Orbit state + input conversion live in the engine now (gldx::OrbitCamera):
    // 19 demos used to spell this out, with three different drag sensitivities and
    // two different pitch clamp ranges. The `target` is still driven here because
    // `--target-y` belongs to this demo's framing, not to the camera maths.
    gldx::OrbitCamera orbit;
    // This demo's framing, spelled out rather than inherited: the engine defaults
    // (yaw .7 / pitch .3 / distance 8) are a generic three-quarter view, and taking
    // them silently moved every frozen frame this demo is verified against.
    View() {
        orbit.settings().yaw = 0.62f;
        orbit.settings().pitch = 0.17f;
        orbit.settings().distance = 3.6f;
        orbit.settings().target = glm::vec3(0.0f, 0.92f, 0.0f);   // sternum height
        orbit.settings().minDistance = 0.8f;
        orbit.settings().maxDistance = 30.0f;
    }

    float fov      = 42.0f;
    float speed    = 1.30f;   // metres per second of floor travel
    // `--hide-mat N`: skip every mesh bound to material N. Diagnosis only - when a
    // patch of the model is the wrong colour, the question is *which draw* paints
    // it, and the fastest answer is to delete that draw and look again. Reading the
    // material table cannot answer it: MMD models routinely ship a dozen materials
    // that all have white base colours and the colour arrives from the texture, so
    // "who is red here" has no textual answer.
    int hideMaterial = -1;
    // `--only-mat N`: the converse - draw nothing but material N. Together with
    // --hide-mat it turns "which mesh is this pixel" into two snapshots instead of
    // a guess.
    int onlyMaterial = -1;
    // `--on allparts`: draw the parts the asset switched off (PMX diffuse alpha 0).
    // Default off, because the author's default look is the reference this demo is
    // verified against; this switch exists to A/B that decision.
    bool allParts = false;
    // What the number keys asked for. Applied to the machine inside the frame loop,
    // never from a callback (see the OnKey comment).
    Action requestAction = Action::Walk;
    // When that request was made, in animation seconds, and how far into the clip we
    // are. One-shot actions (attack/jump) auto-return to idle, and that decision has
    // to be measured from the REQUEST - see the frame loop.
    float actionStartedAt = 0.0f;
    float actionElapsed   = 0.0f;
    unsigned requestSerial = 0;      // see Request()
    unsigned lastSentSerial = 0;     // what the frame loop has already forwarded
    // One dt per frame for the state machine, and the "have we Set() it yet" flag.
    float lastAnimSeconds = 0.0f;
    bool  machineInitialised = false;

    // Every action switch goes through here (number keys and --action alike), so the
    // "when did this start" bookkeeping cannot be forgotten at one of the two call
    // sites - which is exactly how the number keys came to do nothing for attack/jump.
    // `now` is passed in rather than read from `clock`: the key callback fires
    // between frames, and `clock` is only refreshed inside the frame loop - so a
    // request made from a callback would stamp the PREVIOUS frame's time (and at
    // startup, zero). One frame of error is invisible; inheriting a stale or zero
    // timestamp is not, because the one-shot auto-return compares against it.
    void Request(Action a, double now) {
        requestAction = a;
        actionStartedAt = static_cast<float>(now);
        actionElapsed = 0.0f;
        // Bumped on every explicit request. The frame loop compares it with the value it
        // last forwarded to the machine, so a key press reaches the machine exactly once
        // even though the request state persists for the whole action.
        ++requestSerial;
    }

    float freezeAt = 0.0f;    // > 0 pins the clock, for reproducible snapshots
    double clock   = 0.0;     // animation seconds (already clamped by freezeAt)
    float scroll   = 0.0f;    // metres of floor travelled
    glm::vec3 target{0.0f, 0.92f, 0.0f};

    bool walk      = true;
    bool ground    = true;
    bool maps      = true;    // sample the FBX's embedded maps (else flat material)
    bool normalMap = true;    // ...including the normal map (else geometry normals only)
    bool wireframe = false;
    bool showHud    = true;
    bool report     = false;  // dump the gait's joint angles and the per-action table once
    bool trace      = false;  // dump a 16 row time trace of the selected action once

    float groundLift = 0.0f;   // metres the whole character was shifted this frame
    float airRise    = 0.0f;   // metres of that shift that came from a jump arc
    float soleHeight = 0.0f;   // metres, lowest sole after the shift (0 = touching)

    bool dragging = false;
    double lastX = 0.0;
    double lastY = 0.0;
    int drawCalls = 0;
};

}  // namespace anim
