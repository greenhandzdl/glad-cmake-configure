/**
 * @file main.cpp
 * @brief anim-rigid - a rigged FBX, skinned on the GPU, walking forever.
 *
 * What is left in this file after the engine grew its animation surface: the
 * *gait*. Import, the semantic rig classification, the world-axis pose algebra,
 * the ground solve and the Stage-B upload all live in gldx now (RigProfile, Pose,
 * GroundContact, RiggedModel), because none of them are specific to walking -
 * every rigged demo needs the same four things and would otherwise re-derive
 * them, including the three that fail silently (see RiggedModel.h).
 *
 * What is genuinely a demo, and therefore stays here:
 *   - the tuning values handed to the engine's walk source (GaitTuning is now an
 *     alias of gldx::WalkTuning), and the LegAt table `report` prints,
 *   - the treadmill, so the character walks in place and the only thing moving
 *     is the gait,
 *   - the shaders (single sun, no IBL/shadow: the small material this scene can
 *     actually back - see Shaders.h).
 *
 * The asset has a skeleton and skin weights but **no animation clips at all**
 * (17 skinned meshes, 23 bones, zero `AnimationStack`s). So the walking is
 * authored here, procedurally, from the rig's own rest pose: nothing is played
 * back, the pose is a pure function of the clock. That is also why `--freeze-at`
 * frames are reproducible - the gait reads `min(time, freezeAt)` instead of
 * accumulating a dt, so two runs with the same switches draw the same pixels.
 *
 * Three pieces make the skinning work, and each of them is a place this can go
 * silently wrong, so they are worth naming before the code:
 *
 *  1. Space. Assimp hands back the FBX's vertices in each mesh node's local
 *     space, but the cluster bind matrices in bind/scene space. The FBX here is
 *     authored Z-up with the Y-up conversion parked on the mesh nodes, so
 *     `ModelLoader::LoadSkinned` bakes that node transform into the vertices.
 *     Without it the skeleton stands at 90 degrees to its own skin.
 *  2. Rest pose. The skeleton's rest locals are derived from the inverse-bind
 *     matrices, not from the file's node transforms (the two disagree on this
 *     asset - see Skeleton.h). With an empty pose every palette entry equals the
 *     root transform, i.e. the mesh draws as its bind pose by construction.
 *  3. Axes. A walk cycle is authored as rotations about *world* axes (swing the
 *     thigh about the lateral axis). Each bone's own axes come from the rig, so
 *     `PoseRotation()` converts the world axis into that bone's local frame with
 *     its rest world matrix. Nothing about "which axis is lateral" is hardcoded:
 *     it is measured from the rig's bone positions at load time (RigProfile).
 *
 * The character walks in place on a scrolling floor (a treadmill). A treadmill
 * instead of a moving character keeps the framing fixed and makes the walk
 * cycle the only thing changing, which is what this demo is about.
 *
 * Controls: W walk on/off, G floor on/off, F wireframe, H HUD, drag orbits,
 * wheel zooms, Esc quits.
 *
 * Features (all via `--on`/`--off a,b`: `--walk` is an unknown argument, not a
 * switch): hud, walk, ground, wireframe, maps, normalmap, report. `report`
 * prints a 16 row gait table to stderr - the one thing the HUD cannot show,
 * since it is about angles over a whole cycle rather than about this frame.
 *
 * Options: model, speed, fov, yaw, pitch, distance, freeze-at, target-y.
 * `--target-y` moves the orbit centre (default 0.92 m, roughly the sternum) and
 * is what makes the arm/hand clearance shots possible at all.
 *
 * The four embedded maps, the arm chain and the ground solve are each reported
 * on stderr / in the HUD rather than left to "it looks right": `textures: ...`
 * says whether the FBX really carried the maps, the HUD says how many draws the
 * character costs and how many millimetres the ground solve moved the pelvis,
 * and `--on report` dumps the gait. Every claim in the report was measured with
 * those three, not eyeballed.
 *
 * Snapshot discipline (the one thing that silently ruins an A/B here):
 * `GLDX_SNAPSHOT_AT` must be LATER than `--freeze-at`, not equal to it. With the
 * same value the hook fires on a frame where the clock is still running, and two
 * runs of the identical command line differ - which reads like a nondeterministic
 * renderer rather than a framing mistake.
 */

#include "gldx/core/Platform.h"

// Global-module headers first, then the three imports (AGENTS.md invariant 7):
// re-parsing glm/std after `import gldx;` is what makes MSVC answer with C2011
// redefinitions, and clang shows nothing at all.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

import gldx;
import gldxwin;
import gldxcli;

#include "MmdNaming.h"
#include "Tunings.h"

#include "Shaders.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

// The GLSL declares `uniform mat4 uBones[N]` because a uniform array needs a
// literal size. Pinning the engine's number to it here means a rig that outgrows
// the shader fails the build instead of indexing past the end of the array.
static_assert(gldx::kMaxBonePalette == 254, "uBones[N] in Shaders.h must match gldx::kMaxBonePalette");

// The asset this demo is built around: SKINNED, textured, and named in a family the
// demo overrides (MMD/PMX), which is what makes the naming channel testable by just
// running the binary. It also carries the optional skirt/hair chains.
//
// `demo_should_be_ignored.fbx` used to be the default, but it is a placement slot that
// the repo deliberately does NOT ship, so `./output/anim-rigid` with no arguments used
// to open on a load error - and every HUD line then read as "nothing works" rather
// than "no model". A default that only works when the user passes --model is not a
// default.
constexpr const char* kDefaultModel = "src/assets/models/aigei/Lamy_Idol_Mark2_1.0.pmx";

// ---------------------------------------------------------------------------
// The gait. This is the part that is actually this demo's - everything it reads
// comes from the engine's RigProfile (which bone, which axis) and writes into an
// engine Pose. Keeping the numbers together is what makes the walk tunable
// without touching the plumbing around it.
// ---------------------------------------------------------------------------
// The demo used to keep its own copy of every one of these numbers. They now live
// in the engine (WalkTuning), because the curve SHAPE is general humanoid knowledge
// even though these particular VALUES were tuned on this asset. Aliasing rather
// than copying is the point: two structs with the same fields would drift the day
// someone adds one to the engine.
using GaitTuning = gldx::WalkTuning;


// A clamp, an ease and a segment ramp - the three primitives the curves below
// are built from. Naming them keeps the gait readable as a gait instead of as
// arithmetic.
float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float Ease01(float x) {
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
float Segment01(float x, float a, float b) {
    return Clamp01((x - a) / std::max(b - a, 1e-5f));
}

// One leg's joint angles at `p` in [0,1), where 0 is heel strike.
//
// The sign conventions all come from one place: a positive rotation about the
// rig's lateral axis swings a downward-pointing bone backwards. So a thigh angle
// is negative when the leg is forward, knee flexion is positive (the shin goes
// back), and a positive ankle angle points the toes down.
struct LegAngles {
    float hip = 0.0f;
    float knee = 0.0f;
    float ankle = 0.0f;
    float toe = 0.0f;
};

LegAngles LegAt(const GaitTuning& tune, float p) {
    LegAngles out;
    if (p < tune.stanceEnd) {
        const float s = p / tune.stanceEnd;                   // 0 = contact, 1 = toe off
        // Linear through stance, not eased: the foot is planted, so the pelvis
        // passes over it at constant speed and the *relative* hip angle is what
        // is linear here. Easing it would make the character surge.
        out.hip = tune.hipSwing * (2.0f * s - 1.0f);
        // Loading response: the knee dips right after contact and is straight by
        // mid-stance, when the body is highest.
        out.knee = tune.kneeStanceFlex * (1.0f - Ease01(Segment01(s, 0.0f, 0.45f)));
        // Heel strike (toes up) rolls flat, then plantarflexes into push-off.
        out.ankle = tune.ankleHeelStrike * (1.0f - Ease01(Segment01(s, 0.0f, 0.30f))) +
                    tune.ankleToeOff * Ease01(Segment01(s, 0.62f, 1.0f));
        // The toe joint only moves at the very end of stance, when the heel is
        // already off the floor and the toes are the last contact point.
        out.toe = -tune.toeOffBend * Ease01(Segment01(s, 0.70f, 1.0f));
    } else {
        const float s = (p - tune.stanceEnd) / (1.0f - tune.stanceEnd);  // 0 = toe off, 1 = contact
        // Cosine ease: a swinging limb accelerates through the middle and
        // decelerates into the next contact, which is exactly a cosine.
        const float from = tune.hipSwing;
        const float to = -tune.hipSwing;
        out.hip = from + (to - from) * (0.5f - 0.5f * std::cos(kPi * s));
        // Knee: flex hard early in the swing to clear the floor, then extend so
        // the shin is straight and reaching at contact. The two multipliers that
        // force it to zero at both ends are what keep the curve continuous across
        // the stance/swing boundary - a 0.14 rad step there reads as a visible pop.
        // Peaked EARLY in the swing, not in the middle: the knee flexes to lift
        // the foot clear while the leg is still behind the body, then extends as
        // it passes underneath. Peaking at mid-swing is what makes a procedural
        // walk look like marching - the foot arrives under the hips at its highest
        // point, which no one does, and the measured sole height says so (0.31 m
        // before this curve was moved, ~0.06 m after).
        const float swell = Ease01(Segment01(s, 0.0f, 0.22f)) *
                            (1.0f - Ease01(Segment01(s, 0.28f, 0.72f)));
        out.knee = tune.kneeSwingFlex * swell;
        // Plantarflexion unwinds after toe-off; dorsiflexion comes in for the
        // heel-first landing.
        out.ankle = tune.ankleToeOff * (1.0f - Ease01(Segment01(s, 0.0f, 0.28f))) +
                    tune.ankleHeelStrike * Ease01(Segment01(s, 0.40f, 0.95f));
        out.toe = -tune.toeOffBend * (1.0f - Ease01(Segment01(s, 0.0f, 0.22f)));
    }
    return out;
}


} // namespace

namespace {

// Which action is being asked for. A free enum in the demo namespace rather than a
// member of Character: the input callback records it into the View, the frame loop
// turns it into a machine Request. Keeping it out of Character is what lets the
// callback stay free of the animation machine.
enum class Action { Idle, Walk, Run, Crouch, CrouchWalk, Attack, Jump, Sit, Swim };

// ---------------------------------------------------------------------------
// The demo's own state around one character: everything that is NOT the rig
// itself. The rig (skeleton, meshes, maps, unit scale, rest worlds) is
// gldx::RiggedModel; the ground solve is gldx::GroundContact; the semantic bone
// lookup is gldx::RigProfile. What stays here is the scratch they write into
// and the walk-specific numbers derived from them.
// ---------------------------------------------------------------------------
/**
 * Which naming family resolves this asset's bone names.
 *
 * `Auto` is the old behaviour: the engine's built-in general-purpose families
 * (Mixamo/Blender, 3ds Max Biped, UE) compete on Detect(). `Mmd` selects the
 * demo-side MMD/PMX family, which is the only place that knows Japanese bone names.
 *
 * The point of having this as a runtime switch rather than a compile-time pick is
 * that the *same* pose sources must drive both rigs: swapping the model plus this
 * channel is the whole difference.
 */
enum class NamingChannel { Auto, Mmd };

struct Character {
    gldx::RiggedModel model;
    gldx::RigProfile  rig;
    gldx::GroundContact ground;

    std::vector<glm::mat4> pose;      // one local matrix per bone, rebuilt per frame
    std::vector<glm::mat4> palette;   // what the vertex shader reads as uBones[]
    GaitTuning tune;

    // The ground solve's lift for the *rest* pose, in metres. This is the height at
    // which an upright, straight-legged character stands on this floor, and it is not
    // zero (measured: +11.6 mm) because the contact points are the sole plane, which
    // sits a little below the rig origin. Flight needs it: while both feet are off the
    // floor there is nothing to solve against, so a jump's root motion has to be
    // measured from this height rather than from "whatever the solve last said". The
    // jump's take-off and touch-down frames are authored with straight legs - i.e.
    // exactly the rest pose - so the airborne branch and the grounded branch meet at
    // this number with no step. Measured at load, once.
    float restLift = 0.0f;
    // Lowest bind-pose vertex measured along `rig.up` (rig units). The sole plane sits
    // this far below the bone positions; GroundContact needs it, and so does the
    // floor-violation check (see the pose table).
    float bindLowestOnUp = 0.0f;
    // Hip height a seated pose should sit at, in METRES, and the rise that gets it there
    // from the standing height. Sitting is not ground-solved (see GroundWeightFor): its
    // height is authored, because the contact that matters is the seat and the engine's
    // solve only knows the soles. Keeping the number here (metres, world) avoids the
    // unit trap that a pelvis translation inside a pose source would hit - poses are in
    // RIG units (x0.01 on this asset), so 0.87 m written there is 8.7 mm.
    // ABSOLUTE world height of the hips while seated, in metres.
    //
    // This is NOT "the hip height above the floor" - it goes through
    // `sitRise = seatedHip - (hips.y + restLift)`, and for a non-solved pose the final
    // shift is `restLift + sitRise = seatedHip - hips.y`. `hips.y` is the REST hip
    // height (~0.99 m here), so any value below it pushes the whole character UNDER
    // the floor - which is exactly what 0.19/0.22/0.51 did (measured shift -0.76 m,
    // head at -0.60 m). Sitting needs `seatedHip` slightly ABOVE the standing hip
    // height so the pelvis drops by the right amount and no more.
    float seatedHip  = 0.295f;
    // Is `seatedHip` an authored constant, or a fraction of this rig's own leg length?
    //
    // It was a constant, and a constant cannot serve two rigs: 0.295 m is the right
    // seated pelvis height for the 1.68 m reference character (thigh + shin ~0.79 m)
    // and the wrong one for this 2.10 m MMD rig, where it drove the soles 90 mm
    // through the floor and the report said `sit <- SINKS`.
    //
    // Seated height is a RATIO of the leg: fold the shin back and the pelvis comes to
    // rest about a thigh's thickness above the floor, which scales with the body.
    // Deriving it keeps `--action sit` correct on any rig the naming family binds,
    // while the constant stays as the reference value this demo was tuned against -
    // kSeatedHipRatio is the ratio that reproduces 0.295 on a 0.79 m leg
    // (0.295 / 0.79 = 0.373).
    static constexpr float kSeatedHipRatio = 0.373f;
    bool seatedHipDerived = false;
    NamingChannel namingChannel = NamingChannel::Auto;

    // One solver per chain kind. They hold per-joint angular state across frames, so
    // they are members of the character rather than locals: a skirt that forgot its
    // velocity between frames would snap to the target every frame and read as rigid.
    gldx::ChainSolver chainSolvers[gldx::RigChainKindCount];

    // Seconds since the previous secondary-motion step, already clamped. Kept separate
    // from the machine's dt because the solver is only valid on a real time step: a
    // frame that loads the model blocks for seconds and would integrate a spring over
    // that whole interval.
    float secondaryDt = 1.0f / 60.0f;
    bool  secondaryPrimed = false;

    // Per-kind chain parameters. Values are asset properties (how many panels, how
    // soft), so they live here rather than in the engine - same rule as WalkTuning.
    gldx::ChainTuning chainTune[gldx::RigChainKindCount];
    float sitRise    = 0.0f;
    // Bind-space corner box of the pelvis, measured once at load. The SEATED solve uses
    // it (see GroundContact::AddBodyContact): sitting puts the weight on the seat, and
    // the four sole points cannot see the seat.


    // The action layer. `walk` is the only source this demo holds, and the machine
    // owns neither it nor the rig - both live right above, so destroying the
    // character in the wrong order is the only way to dangle (see AnimStateMachine.h).
    //
    // Held through unique_ptrs because a ProceduralWalk takes references at
    // construction (restWorld, rig, skeleton), and those only exist after the
    // model has uploaded - so it cannot be a plain member built in step with
    // this struct.
    std::unique_ptr<gldx::ProceduralIdle>   idleSource;
    std::unique_ptr<gldx::ProceduralWalk> walkSource;
    std::unique_ptr<gldx::ProceduralRun>  runSource;
    std::unique_ptr<gldx::ProceduralCrouch> crouchSource;
    std::unique_ptr<gldx::ProceduralWalk>   crouchWalkSource;
    std::unique_ptr<gldx::ProceduralAttack> attackSource;
    std::unique_ptr<gldx::ProceduralJump>   jumpSource;
    std::unique_ptr<gldx::ProceduralSit>    sitSource;
    std::unique_ptr<gldx::ProceduralSwim>   swimSource;

    // Which one-shot action is still running, if any, and whether it has ended.
    //
    // Attack and jump are **events**, not modes: once the swing lands or the feet
    // touch down the character should go back to idling on its own. Without this the
    // last frame stays on screen until the user presses another number, which reads
    // as "it froze" rather than "it finished".
    //
    // Kept as a query rather than a stored "finished" flag so it cannot go stale
    // when the action is changed underneath it.
    [[nodiscard]] bool oneShotFinished(Action a, float t) const {
        const gldx::AnimPoseSource* src = nullptr;
        if (a == Action::Attack) src = attackSource.get();
        else if (a == Action::Jump) src = jumpSource.get();
        return src != nullptr && src->FinishedAt(t);
    }
    // Which AnimPoseSource backs an action, for the machine. Crouch-walking and the
    // static crouch both go through the machine too; the two phase-driven gaits are
    // reported separately because the machine cannot know their cycles (see the frame
    // loop: it samples them at machine time, which is exactly a cycle phase).
    [[nodiscard]] const gldx::AnimPoseSource* sourceFor(Action a) const {
        switch (a) {
            case Action::Idle:       return idleSource.get();
            case Action::Walk:       return walkSource.get();
            case Action::Run:        return runSource.get();
            case Action::Crouch:     return crouchSource.get();
            case Action::CrouchWalk: return crouchWalkSource.get();
            case Action::Attack:     return attackSource.get();
            case Action::Jump:       return jumpSource.get();
            case Action::Sit:        return sitSource.get();
            case Action::Swim:       return swimSource.get();
        }
        return nullptr;
    }

    gldx::AnimStateMachine               machine;



    float cycleMetres = 1.0f;         // floor travel per full two-step cycle
    std::string error;

    [[nodiscard]] bool ready() const { return model.ready(); }
    [[nodiscard]] float phaseFor(float speed, float seconds) const {
        // Distance walked / distance travelled by one cycle, so the feet stay
        // planted: faster walking is a faster cadence, not a longer stride.
        return cycleMetres > 1e-3f ? 2.0f * kPi * (speed * seconds / cycleMetres) : 0.0f;
    }

    // The same cadence expressed in the action layer's unit: one cycle is one
    // Duration(). Kept next to phaseFor (rather than folding one into the other)
    // because `report` prints the cycle in radians and the machine samples in
    // seconds - deriving both from cycleMetres is what keeps them in agreement.
    [[nodiscard]] float cycleSeconds(float speed) const {
        return (cycleMetres > 1e-3f && speed > 1e-6f) ? cycleMetres / speed : 1.0f;
    }

    // Seconds into the walk that correspond to `phaseRadians`. This is exactly the
    // inverse of phaseFor, so sampling the source at this time reproduces the old
    // BuildWalkPose(phase) pose bit for bit.
    // Seconds to feed the source for a given phase, *in the source's own duration
    // units*. The source is built with cycleSeconds(1.0f) and never rebuilt, so this
    // must not involve the runtime speed: the phase already encodes it. Passing a
    // speed here looks harmless and costs 30% of the cycle - the source then wraps at
    // a different period than the phase was measured on.
    //
    // Measured limit of this round trip: the phase is built as
    // `2*pi*(speed*t/cycleMetres)` and inverted back, so a clock difference of exactly
    // one cycle does NOT return the same time to the last bit. At t=0.37 s the folded
    // source time is 0.481000 s; one cycle later it is 0.480950 s and three cycles
    // later 0.480850 s (error grows with t because `speed*t` is where the precision
    // goes first). That is 1e-4 s of phase, which is invisible in the pose itself
    // (max bone delta ~1e-4) but IS enough to move a few percent of pixels at 8-bit:
    // snapshotting `t` and `t + one cycle` therefore shows ~3.4% differing pixels, and
    // that number does not shrink when you fix the period. The four time-driven
    // actions (idle/run/crouch and the non-looping pair) close to 0 px because they
    // are sampled at the raw clock with no phase round trip at all.
    //
    // So: do not use `t` vs `t + cycle` snapshots to regression-test walk's loop
    // closure. Probe the source directly instead (its own drift is 1.2e-7).
    [[nodiscard]] float walkTimeFor(float phaseRadians) const {
        // Algebraically this is just `speed * seconds / cycleMetres` inverted, but it
        // is written through the same two quantities phaseFor uses so that sampling
        // at t = walkTimeFor(phaseFor(t)) round-trips within a float ULP. Doing the
        // fold any other way (e.g. normalising by a Duration set from a *different*
        // speed) shifts every phase slightly, which shows up as a whole-character
        // silhouette change rather than as an obvious error.
        const float cycles = phaseRadians / (2.0f * kPi);
        return cycles * cycleSeconds(1.0f);
    }
};

// ---------------------------------------------------------------------------
// The one place that decides where the character sits vertically, for one action at
// one time. Both the frame loop and the report tables call these, so the numbers the
// panel prints are the numbers the picture is made of. That is not a nicety: the
// report used to measure the pose *before* the ground solve, and it claimed a crouch
// 35 cm lower than the one on screen (0.999 m reported, 1.208 m rendered), which sent
// the previous tuning round chasing a number that no pixel could ever agree with.
// ---------------------------------------------------------------------------

// How much ground contact to apply: 1 = fully grounded, 0 = fully airborne.
//
// A jump is a boolean because it has one clearly defined flight arc; a run is a
// continuous weight because it has two short flights per cycle and a boolean would
// snap the pelvis on the boundary frame.
float GroundWeightFor(const Character& c, Action a, float t) {
    switch (a) {
        case Action::Jump:
            return (c.jumpSource && !c.jumpSource->GroundedAt(t)) ? 0.0f : 1.0f;
        case Action::Run:
            return c.runSource ? 1.0f - c.runSource->AirborneWeightAt(t) : 1.0f;
        case Action::Swim:
            return 0.0f;   // feet do not touch the floor; the water rise owns the height
        case Action::Sit:
            // The support is the SEAT, not the soles, so the sole solve has nothing to
            // say. The height is authored (see Character::seatedHip) and applied through
            // the same channel as a jump's arc.
            return 0.0f;
        default:
            return 1.0f;   // walk / idle / crouch / crouchwalk / attack
    }
}

// Metres to add to every palette translation. `solvedLift` is `ground.Solve()`'s
// answer for the pose the caller just built.
//
// While grounded the solve IS the answer: it puts the lowest sole on the floor and
// the pelvis height falls out of the leg angles. While airborne it means nothing -
// the soles are not supposed to be on the floor - so the flight height is measured
// from `restLift` (where straight legs stand) plus the source's own arc. The jump's
// take-off and touch-down frames are authored with straight legs, i.e. the rest pose,
// so at those two instants `restLift + 0 == solvedLift` and the branches meet with no
// visible step; that is the whole reason the arc is applied here and not inside the
// pose (the pose is in rig units, the arc is in metres).
float VerticalShiftFor(const Character& c, Action a, float t, float solvedLift) {
    const float weight = GroundWeightFor(c, a, t);
    float rise = 0.0f;
    if (a == Action::Jump && c.jumpSource) {
        rise = c.jumpSource->AirborneRiseAt(t);
    } else if (a == Action::Sit) {
        rise = c.sitRise;
    } else if (a == Action::Swim && c.swimSource) {
        // A swimmer does not stand on the floor: the whole body floats at the water
        // line. Applied through the same channel as a jump's arc, so there is exactly
        // one place that decides "how high is this character right now".
        rise = c.swimSource->waterRise();
    } else if (a == Action::Run && c.runSource) {
        // A run's flight is short, so its height is scaled by the same continuous
        // weight that gates the solve - with the weight's slope zero at the boundary,
        // so the pelvis never jumps. Without this term the run's feet stay pinned to
        // the floor while the legs fold, i.e. "running on a treadmill that follows you".
        rise = c.runSource->airborneRiseAt(t);
    }
    const float flight = c.restLift + rise;
    // Written as a weighted sum so the walk path stays bit-identical: weight is
    // exactly 1 there, and 1*x + 0*y is x in IEEE arithmetic.
    return weight * solvedLift + (1.0f - weight) * flight;
}

// Camera + the runtime state the pass, the HUD and the input callbacks share.
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

// The scene's fixed lighting/bounce palette. Linear values; the shaders tone map
// and encode, so these are not the numbers to read off a screenshot.
constexpr glm::vec3 kSkyColor{0.30f, 0.44f, 0.62f};
constexpr glm::vec3 kGroundColor{0.20f, 0.21f, 0.23f};
constexpr glm::vec3 kBounceColor{0.08f, 0.07f, 0.06f};
constexpr glm::vec3 kSunColor{1.00f, 0.94f, 0.84f};
constexpr glm::vec3 kSunDir = {-0.38f, -0.82f, -0.42f};   // travels toward the scene
constexpr float     kSunIntensity = 3.4f;

glm::vec3 AcesFilm(glm::vec3 x) {
    const glm::vec3 a = x * (2.51f * x + 0.03f);
    const glm::vec3 b = x * (2.43f * x + 0.59f) + 0.14f;
    return glm::clamp(a / b, glm::vec3(0.0f), glm::vec3(1.0f));
}

// The linear->display transform both shaders end with, mirrored on the CPU for
// the clear colour so the horizon fade and the background cannot disagree (a
// mismatch there is a visible band exactly where the floor runs out).
glm::vec3 DisplayColor(glm::vec3 linear) {
    return glm::pow(AcesFilm(linear * 1.15f), glm::vec3(1.0f / 2.2f));
}

// ---------------------------------------------------------------------------
// SkinPass: the floor and the character, drawn straight at the window (no
// PostProcessChain - this demo has no HDR/bloom to composite, and the shaders
// encode their own output).
// ---------------------------------------------------------------------------
class SkinPass : public gldx::RenderPass {
public:
    SkinPass(Character& character, View& view)
        : RenderPass("Skin"), character_(character), view_(view) {
        if (auto skin = gldx::ShaderProgram::CreateFromSource(anim_rigid::shaders::kSkinVertex,
                                                             anim_rigid::shaders::kSkinFragment)) {
            skin_.emplace(std::move(*skin));
        } else {
            std::fprintf(stderr, "[anim-rigid] skin shader: %s\n", skin.error().c_str());
        }
        if (auto ground = gldx::ShaderProgram::CreateFromSource(anim_rigid::shaders::kGroundVertex,
                                                               anim_rigid::shaders::kGroundFragment)) {
            ground_.emplace(std::move(*ground));
        } else {
            std::fprintf(stderr, "[anim-rigid] ground shader: %s\n", ground.error().c_str());
        }
        // 80 x 80 m: far enough that the distance fade hides the edge, cheap
        // enough that the whole floor is two triangles.
        groundMesh_.Upload(gldx::GeometryFactory::Plane(40.0f));
    }

    void Execute(gldx::RenderFrame& f) override {
        gldx::RenderContext::AssertRenderThread("SkinPass::Execute");
        // Own the state instead of inheriting it: gldx::SpriteBatch::End() leaves
        // GL_DEPTH_TEST and GL_CULL_FACE disabled (it only restores blending), and
        // the HUD pass drew last frame. Inheriting that is subtle, not loud - the
        // character renders with its own back faces and interior cloth showing
        // through, which reads as "the skinning is broken" at exactly the frame
        // where the HUD is enabled. Each pass in this repo establishes what it
        // draws with; this one draws 3D, so it asks for the 3D state.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glDisable(GL_BLEND);
        const glm::vec3 clear = DisplayColor(kSkyColor * 0.55f);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(clear.r, clear.g, clear.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (f.fbWidth <= 0 || f.fbHeight <= 0) return;

        const glm::vec3 eye = f.camera ? f.camera->Position() : glm::vec3(0.0f, 1.0f, 4.0f);
        int draws = 0;

        if (view_.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        if (view_.ground && ground_ && groundMesh_.valid()) {
            ground_->Use();
            ground_->Set("uViewProj", f.viewProj);
            ground_->Set("uCamPos", eye);
            ground_->Set("uSunDir", glm::normalize(kSunDir));
            ground_->Set("uSunColor", kSunColor);
            ground_->Set("uSunIntensity", kSunIntensity);
            ground_->Set("uSkyColor", kSkyColor);
            ground_->Set("uGroundColor", kGroundColor);
            ground_->Set("uCharPos", glm::vec3(0.0f));
            ground_->Set("uScroll", view_.scroll);
            groundMesh_.Draw();
            ++draws;
        }

        const gldx::RiggedModel& model = character_.model;
        if (skin_ && model.ready() && !character_.palette.empty()) {
            skin_->Use();
            skin_->Set("uViewProj", f.viewProj);
            skin_->Set("uCamPos", eye);
            skin_->Set("uSunDir", glm::normalize(kSunDir));
            skin_->Set("uSunColor", kSunColor);
            skin_->Set("uSunIntensity", kSunIntensity);
            skin_->Set("uSkyColor", kSkyColor);
            skin_->Set("uBounceColor", kBounceColor);
            // The palette convention ("uBones[i]", one entry per bone) belongs to
            // the engine; a caller that spelled these names by hand would get a
            // uniform that silently never gets set.
            model.BindPalette(*skin_, character_.palette);

            // Every sampler must point at a complete texture on every draw, dummy
            // or not: an active sampler with nothing behind it makes Apple's driver
            // drop the whole call (the failure looks like "the model vanished").
            // AlbedoOrFallback() and friends are that guarantee, so this loop has
            // no null case to handle.
            // ---- per-mesh material binding ----
            //
            // This loop used to bind ONE material (`PrimaryMaterial()`) and its maps
            // before drawing every mesh. On a 60-mesh / 15-material character that is
            // silently wrong: every panel samples the first material's atlas, which on
            // this asset is the near-white skin map - so a fully textured model
            // rendered white, and the few coloured patches that showed through were
            // the one image's non-white corners landing somewhere plausible.
            //
            // The maps and the material scalars are now chosen per mesh through
            // `meshMaterial()[i]`, and the `uHas*` flags move with them.
            const bool useMaps = view_.maps;
            const bool useNormal = useMaps && view_.normalMap;
            const auto& meshMat = model.meshMaterial();
            const auto& materials = model.materials();

            int boundMaterial = -2;   // -2 = nothing bound yet, -1 = out of range
            bool blending = glIsEnabled(GL_BLEND) == GL_TRUE;   // tracked, not assumed off
            if (meshMat.empty()) {
                // A rig with no material list at all: bind the fallbacks once so every
                // sampler still points at a complete texture (Apple drops the draw
                // otherwise) and let the flags switch the sampling off.
                const gldx::Texture2D& albedo = model.fallbackAlbedo();
                const gldx::Texture2D& normal = model.fallbackNormal();
                const gldx::Texture2D& scalar = model.fallbackScalar();
                albedo.Bind(0);
                normal.Bind(1);
                scalar.Bind(2);
                scalar.Bind(3);
                skin_->Set("uAlbedo", 0);
                skin_->Set("uNormal", 1);
                skin_->Set("uMetallic", 2);
                skin_->Set("uRoughness", 3);
                skin_->Set("uHasAlbedo", 0);
                skin_->Set("uHasNormal", 0);
                skin_->Set("uHasMetallic", 0);
                skin_->Set("uHasRoughness", 0);
                boundMaterial = -1;
            }

            for (std::size_t mi = 0; mi < model.meshes().size(); ++mi) {
                const int matIndex = (mi < meshMat.size()) ? meshMat[mi] : -1;

                if (view_.hideMaterial >= 0 && matIndex == view_.hideMaterial) continue;
                if (view_.onlyMaterial >= 0 && matIndex != view_.onlyMaterial) continue;
                // Parts the asset switched off (PMX diffuse alpha 0) stay off: the
                // author's default look is the reference, and these are optional
                // clothing, not missing data. `--on allparts` draws them anyway for
                // comparison.
                if (!view_.allParts && model.isHidden(matIndex)) continue;

                if (matIndex != boundMaterial) {
                    boundMaterial = matIndex;
                    const bool hasAlbedo = useMaps && model.hasAlbedo(matIndex);
                    const bool hasNormal = useNormal && model.hasNormal(matIndex);
                    const bool hasMetallic = useMaps && model.hasMetallic(matIndex);
                    const bool hasRoughness = useMaps && model.hasRoughness(matIndex);

                    model.AlbedoOrFallback(matIndex).Bind(0);
                    model.NormalOrFallback(matIndex).Bind(1);
                    model.MetallicOrFallback(matIndex).Bind(2);
                    model.RoughnessOrFallback(matIndex).Bind(3);
                    skin_->Set("uAlbedo", 0);
                    skin_->Set("uNormal", 1);
                    skin_->Set("uMetallic", 2);
                    skin_->Set("uRoughness", 3);
                    skin_->Set("uHasAlbedo", hasAlbedo ? 1 : 0);
                    skin_->Set("uHasNormal", hasNormal ? 1 : 0);
                    skin_->Set("uHasMetallic", hasMetallic ? 1 : 0);
                    skin_->Set("uHasRoughness", hasRoughness ? 1 : 0);

                    if (matIndex >= 0 && static_cast<std::size_t>(matIndex) < materials.size()) {
                        const gldx::SkinnedMaterial& m = materials[static_cast<std::size_t>(matIndex)];
                        if (std::getenv("GLDX_MAT"))
                            std::fprintf(stderr, "[mat] %d base=(%.2f %.2f %.2f %.2f) alb=%d\n",
                                         matIndex, (double)m.baseColor.r, (double)m.baseColor.g,
                                         (double)m.baseColor.b, (double)m.baseColor.a,
                                         m.albedoTexture);
                        skin_->Set("uBaseColor", m.baseColor);
                        skin_->Set("uMetallicFactor", m.metallic);
                        skin_->Set("uRoughnessFactor", m.roughness);
                    }
                }

                if (std::getenv("GLDX_DRAWMESH")) {
                    static int shown = 0;
                    if (shown++ < 25)
                        std::fprintf(stderr, "[draw] mesh %zu mat=%d verts=%zu\n", mi, matIndex,
                                     model.meshes()[mi].vertexCount());
                }

                // ---- alpha blending, per material ----
                //
                // MMD face models paint blush, eyeliner and the mouth line on one
                // mostly-transparent sheet laid over the head. Drawn opaque it is a
                // solid-band tattoo; drawn blended it is a soft wash. The blend state
                // is ours to set because this pass owns GL state, so it has to be set
                // per mesh, not once (one character legitimately mixes both).
                //
                // Depth writes are left OFF for blended meshes: that sheet sits a few
                // millimetres off the skin, and with depth writes on it would occlude
                // the eyes drawn later in the same frame. It is painted geometry, not
                // a solid surface, so it has no business in the depth buffer.
                const bool blended = useMaps && model.isBlended(matIndex);
                if (blended != blending) {
                    blending = blended;
                    if (blended) {
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        glDepthMask(GL_FALSE);
                    } else {
                        glDisable(GL_BLEND);
                        glDepthMask(GL_TRUE);
                    }
                }
                // The blend equation multiplies by the FRAGMENT alpha, so switching
                // GL_BLEND on is only half the job - the shader has to emit the
                // texture's alpha rather than a constant 1.0. Set on every mesh
                // because the value follows the material, not the pass.
                skin_->Set("uBlended", blended ? 1 : 0);

                model.meshes()[mi].Draw();
                ++draws;
            }

            // Pass-local GL state is restored, never inherited: the next pass (the HUD,
            // in particular) draws its glyphs blended and assumes it is the one turning
            // blending on.
            if (blending) {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
        }

        if (view_.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);   // pass-local state is restored
        view_.drawCalls = draws;
    }

private:
    Character& character_;
    View&      view_;
    std::optional<gldx::ShaderProgram> skin_;
    std::optional<gldx::ShaderProgram> ground_;
    gldx::Mesh groundMesh_;
};

// ---------------------------------------------------------------------------
// The panel: what the demo asserts (walking, skinned) plus the evidence a
// reviewer would otherwise have to infer from the picture (bone count, palette
// fill, the bind-pose check, the phase the frame was frozen at).
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
            "0 idle  1 walk  2 run  3 crouch  4 crouchwalk  5 attack  6 jump  7 sit  8 swim",
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
void UploadCharacter(Character& character, gldx::LoadedSkinnedModelData&& data,
                     bool reportGait, const char* traceAction) {
    // Classification has to happen BEFORE the upload, and it is the reason this
    // function is shaped the way it is. RiggedModel refuses to own the exporter
    // name table (two tables that disagree bind a rig halfway - see its file
    // comment), so the model cannot derive orientation or the sole plane itself;
    // both need RigProfile, which needs the Skeleton, which arrives inside `data`.
    //
    // So: classify the skeleton in place, measure the two numbers that depend on
    // the *measured* up axis, hand those to the model, and only then move the data.
    // The naming family is chosen **here, in the demo** - the engine only knows the
    // interface. `--naming mmd` forces the MMD/PMX family; anything else (default
    // "auto") lets the families compete by their own `Detect()` score. This is the
    // "channel" the task asks for: the same pose sources drive a Mixamo-named rig and
    // an MMD-named one, and only this line differs.
    //
    // `auto` used to pass nullptr, i.e. the ENGINE's built-in families only - so the
    // demo-side MMD family could never win an auto-detect, and the channel was really
    // a two-way switch: "auto (everything except MMD)" vs "MMD". The visible cost: the
    // same 240-bone character bound 20/20 as a .pmx and 0/20 as a .fbx, because the
    // extension test only recognises `.pmx` and the caller had to know to pass
    // `--naming mmd` by hand. A family that owns `Detect()` should be allowed to answer
    // it; the extension test below stays as a cheap hint, not as the only path.
    const gldx::RigNaming* naming = nullptr;
    if (character.namingChannel == NamingChannel::Mmd) {
        naming = &animrigid::MmdNaming::Instance();
    } else {
        // Ask the demo family how well it recognises this skeleton and take it when the
        // match is near-total. 15/20 is deliberately not "any match wins": a rig that
        // shares a handful of names is more likely to be a different convention, and
        // binding one wrongly is silent (a wrong bone still animates something).
        const int mmdScore = animrigid::MmdNaming::Instance().Detect(data.skeleton);
        if (mmdScore >= 15) {
            naming = &animrigid::MmdNaming::Instance();
            std::fprintf(stderr,
                         "[anim-rigid] naming: auto-detected MMD/PMX (%d/20 slots)\n", mmdScore);
        }
    }
    character.rig = gldx::RigProfile::Classify(data.skeleton, naming);
    if (std::getenv("GLDX_CHAINS")) {
        for (int ci = 0; ci < gldx::RigChainKindCount; ++ci) {
            std::fprintf(stderr, "[chain] kind=%d count=%d\n", ci, character.rig.chains[ci].count);
        }
    }
    // `GLDX_BONES=sub`: every bone whose name contains `sub`, in skeleton order.
    // The chain lookups build names by concatenation, so "why did this bone not match"
    // is only answerable by printing the names the skeleton actually holds - assimp is
    // free to rewrite them, and a mismatch looks identical to a missing bone.
    if (const char* sub = std::getenv("GLDX_BONES")) {
        for (std::size_t i = 0; i < data.skeleton.bones().size(); ++i) {
            const std::string& nm = data.skeleton.bones()[i].name;
            if (nm.find(sub) != std::string::npos)
                std::fprintf(stderr, "[bone] %3zu %s\n", i, nm.c_str());
        }
    }
    if (std::getenv("GLDX_DUMP_JOINTS")) {
        auto show = [&](const char* label, int bone) {
            if (bone < 0) { std::fprintf(stderr, "[joint] %-10s <unbound>\n", label); return; }
            const glm::mat4 rw = data.skeleton.RestWorld(bone);
            const glm::vec3 p(rw[3]);
            std::fprintf(stderr, "[joint] %-10s idx=%-3d pos=(%+.3f %+.3f %+.3f)\n",
                         label, bone, (double)p.x, (double)p.y, (double)p.z);
        };
        show("hips", character.rig.hips);
        show("spine", character.rig.spine);
        show("chest", character.rig.chest);
        show("head", character.rig.head);
        show("legUp.L", character.rig.legUp[gldx::kRigLeft]);
        show("legLow.L", character.rig.legLow[gldx::kRigLeft]);
        show("foot.L", character.rig.foot[gldx::kRigLeft]);
    }

    // Where the sole plane is: the lowest bind-pose vertex projected on the rig's
    // up axis. It has to come from the VERTICES, not from the bones - the ankle
    // joint sits several centimetres above the sole, so projecting a contact from
    // the joint instead of the sole would float the whole character by that offset
    // and nothing would report it.
    //
    // Measured BEFORE the model takes ownership of `data`: after the move, all of
    // this is gone. (Getting that order wrong does not fail to compile - it leaves
    // a reference to a moved-from Skeleton, whose bone list is empty while the
    // RigProfile still holds valid-*looking* indices, so the very next RestWorld()
    // reads off the end. That is the crash this comment exists to prevent.)
    bool firstVertex = true;
    for (const gldx::SkinnedMeshData& mesh : data.meshes) {
        for (const gldx::SkinnedVertex& v : mesh.vertices) {
            const float d = glm::dot(v.position, character.rig.up);
            character.bindLowestOnUp = firstVertex ? d
                                                    : std::min(character.bindLowestOnUp, d);
            firstVertex = false;
        }
    }

    // Hand the classification in rather than letting Upload redo it: we already
    // have one, and Upload's leg-length measurement needs exactly this answer.
    character.model.Upload(std::move(data), &character.rig);
    const gldx::RiggedModel& model = character.model;
    if (!model.ready()) {
        character.error = "the file carried no skinned mesh";
        return;
    }
    // The skeleton the rig indices were classified against, now owned by the model.
    const gldx::Skeleton& skeleton = model.skeleton();
    // Rig axes -> world basis, so the character stands up whatever axis its author
    // used. Set after Upload because Upload is what the GL calls belong to; the
    // orientation is pure value math and could be set any time before the first
    // draw, but this is the one place that has a RigProfile to give it.
    character.model.SetOrientation(character.rig.side, character.rig.up, character.rig.forward);


    character.ground = gldx::GroundContact(
        skeleton, character.rig.up,
        {character.rig.foot[gldx::kRigLeft], character.rig.foot[gldx::kRigRight]},
        {character.rig.toe[gldx::kRigLeft], character.rig.toe[gldx::kRigRight]},
        character.bindLowestOnUp);
    // NOTE: the pelvis contact is NOT added to `ground` (the stand/walk solve). It
    // would be harmless there - a standing pelvis is 0.99 m up, far above the soles,
    // so it never wins the min() - but keeping it out makes the walk path provably
    // unchanged. Only the seated solve uses it (see ResetGroundFor below).

    // Where straight legs stand. Measured once, through the same solve the frame loop
    // uses, so the airborne branch and the grounded branch of VerticalShiftFor meet
    // at the same height with no step on the take-off / touch-down frames.
    {
        std::vector<glm::mat4> restPose;
        gldx::ResetToRest(skeleton, restPose);
        std::vector<glm::mat4> restPalette;
        skeleton.ComputeSkinning(restPose, model.Mat4(), restPalette);
        const gldx::GroundContact::Solution s = character.ground.Solve(restPalette);
        character.restLift = s.valid() ? s.lift : 0.0f;
        // Where a standing pelvis actually ends up (rest pose + its lift), so the seated
        // height below can be stated as an absolute number rather than a delta from a
        // number nobody remembers.
        //
        // `rig.hips == -1` means the naming family did not recognise this rig at all
        // (an MMD/PMX-named asset does not match Mixamo/Biped/UE). `skeleton.bone(-1)`
        // is nullptr and dereferencing it is what produced EXC_BAD_ACCESS at 0x4c;
        // the demo must still run, just without a believable seated height.
        if (character.rig.hips >= 0) {
            // Scale the seated height to this rig before it is used. Without this the
            // authored 0.295 m is applied verbatim to a 2.10 m MMD body whose folded
            // legs are far taller, and the soles end up under the floor.
            character.seatedHip = model.legLengthMetres() * Character::kSeatedHipRatio;
            character.seatedHipDerived = true;
            const glm::vec4 hips =
                restPalette[(std::size_t)character.rig.hips] *
                glm::vec4(-glm::vec3(skeleton.bone(character.rig.hips)->inverseBind[3]), 1.0f);
            character.sitRise = character.seatedHip - (hips.y + character.restLift);
        }
    }

    // One cycle = two steps, and one step lays down the horizontal distance the
    // foot covers between its extremes: 2 * legLength * sin(hipSwing). The floor
    // scroll and the cadence both come from this, which is what keeps the feet
    // from skating.

    // The action source needs the skeleton and the rest-world cache, so it can only
    // be built here - after Upload has taken ownership of `data` and the model is
    // the one holding both. Duration is left at one cycle at 1 m/s; the machine is
    // driven by explicit times below, and a caller that steps the machine instead
    // would want cycleSeconds(speed) here.
    // Duration is the cycle at the demo's *current* speed, and both the construction
    // and every sample go through cycleSeconds(view.speed). Building it at a
    // different speed than the one used to convert phases is a silent 30% error in
    // the fold: the source would wrap at the wrong period and every frame would land
    // on a slightly different phase than the phase it was asked for.
    //
    // (Rebuilding the source on a speed change would be the alternative, but speed is
    // a view parameter and the source only needs its Duration to agree; passing the
    // matching time is enough, as below.)
    // ---- per-asset numbers, actually wired ----
    //
    // `TuningsFor()` decided which asset family this is and returned the matching
    // tuple, but nothing ever CALLED it: every source below was constructed with a
    // default-constructed `*Tuning{}`, so the whole per-asset layer in Tunings.h was
    // dead code and the engine's defaults - tuned on the reference skirt character -
    // drove every rig. That is why "MMD looks wrong and re-tuning did nothing".
    // Which tuple: ask whose naming family classified this skeleton. Comparing the
    // family NAME (rather than its first letter, or bone count, or any other proxy) is
    // the only version that cannot silently pick wrong for the next asset - the string
    // is what the report line prints, so the two can never disagree.
    // ONLY the MMD family gets a tuple. Everything else keeps the engine defaults,
    // which is exactly what it ran before this wiring existed.
    //
    // `CoarseSkinnedTunings()` is deliberately NOT applied to the other rigs. Those
    // numbers were written for one asset and never called - so that asset's rendered
    // frames were produced with the engine defaults, and switching it over now would
    // silently rewrite a baseline that other work is verified against (measured: the
    // frozen frame moved bbf2e477 -> 4d4691cc). Re-tuning an asset is a decision that
    // needs its own before/after, not a side effect of making a dead function live.
    const bool mmdFamily =
        std::string_view(character.rig.namingFamily()) == animrigid::MmdNaming::Instance().family();
    const animrigid::PoseTunings tunings =
        mmdFamily ? animrigid::TuningsFor(animrigid::NamingChannelForTunings::Rigid)
                  : animrigid::PoseTunings{};
    character.tune = tunings.walk;
    // Recompute the cycle length AFTER the per-asset numbers land. It derives from
    // `tune.hipSwing`, so computing it before that assignment measures the engine
    // default while the gait then runs with the asset's own swing - the floor scrolls
    // for a stride the legs are no longer taking, and the report says `walk <- SKATES`
    // (measured on the coarse-weight rig: foot 0.415 vs expected 0.583).
    character.cycleMetres =
        std::max(4.0f * model.legLengthMetres() * std::sin(character.tune.hipSwing), 0.1f);
    character.idleSource = std::make_unique<gldx::ProceduralIdle>(
        skeleton, character.rig, model.restWorld(), tunings.idle);
    character.walkSource = std::make_unique<gldx::ProceduralWalk>(
        skeleton, character.rig, model.restWorld(), character.tune,
        character.cycleSeconds(1.0f));
    // The other four actions. Their durations are independent of the floor speed:
    // a run cycle is 0.72 s whatever the treadmill does (speed changes cadence in
    // this demo, and the run's own cadence is fixed), and jump/attack are events
    // with a fixed length.
    character.runSource = std::make_unique<gldx::ProceduralRun>(
        skeleton, character.rig, model.restWorld(), tunings.run, 0.72f);
    // Crouch-walking is walk's own curve with `crouch = 1`, not a separate action:
    // same gait, narrower stride, knees held bent. Making it a second set of curves
    // is how the two drift apart. The pelvis drop is still solved from ground contact,
    // so nothing here hard-codes a height.
    {
        gldx::WalkTuning crouchTune = character.tune;
        crouchTune.crouch = 1.0f;
        character.crouchWalkSource = std::make_unique<gldx::ProceduralWalk>(
            skeleton, character.rig, model.restWorld(), crouchTune,
            character.cycleSeconds(1.0f));
    }
    // Crouch is crouch-WALK's own curve held at one phase (see ProceduralCrouch.h):
    // the two actions cannot drift apart because there is only one curve. `report`
    // prints the byte-level delta between them so "same pose" stays a measurement.
    character.crouchSource = std::make_unique<gldx::ProceduralCrouch>(
        skeleton, character.rig, model.restWorld(), tunings.crouch, character.tune);
    character.attackSource = std::make_unique<gldx::ProceduralAttack>(
        skeleton, character.rig, model.restWorld(), tunings.attack);
    character.jumpSource = std::make_unique<gldx::ProceduralJump>(
        skeleton, character.rig, model.restWorld(), tunings.jump);
    character.sitSource = std::make_unique<gldx::ProceduralSit>(
        skeleton, character.rig, model.restWorld(), tunings.sit);
    character.swimSource = std::make_unique<gldx::ProceduralSwim>(
        skeleton, character.rig, model.restWorld(), tunings.swim);
    // Face down. The sign is the difference between freestyle and backstroke, and it
    // now lives in the pose source so it shares a frame with the arm targets.
    character.swimSource->SetProneAngle(1.55f);
    // Chain parameters, one set per kind. Numbers are per-asset: the skirt here is a
    // 12-panel MMD hem, which is much softer than a tailor-made cape.
    {
        using gldx::RigChainKind;
        auto& t = character.chainTune;
        t[static_cast<int>(RigChainKind::Skirt)] = gldx::ChainTuning{0.22f, 0.88f, 0.30f};
        t[static_cast<int>(RigChainKind::Hair)]  = gldx::ChainTuning{0.30f, 0.85f, 0.18f};
        t[static_cast<int>(RigChainKind::Tail)]  = gldx::ChainTuning{0.28f, 0.86f, 0.12f};
        t[static_cast<int>(RigChainKind::Ear)]   = gldx::ChainTuning{0.45f, 0.80f, -0.10f};
    }

    character.machine.Set(character.walkSource.get());

    // The number keys / --action pick a source directly instead of going through the
    // machine: the machine exists to *blend* between actions, and a frozen single-
    // action frame that is mid-transition is not a useful reference image. The
    // machine's four-transition path is exercised by the probe, not by the demo.

    std::fprintf(stderr,
                 "[anim-rigid] %s: %zu bones, %zu meshes, %zu verts (%zu tris), height %.2f m, "
                 "palette %zu/%d, bind check %.3e\n",
                 model.sourcePath().c_str(), skeleton.size(), model.meshes().size(),
                 model.vertexCount(), model.triangleCount(),
                 static_cast<double>(model.heightMetres()), skeleton.size(),
                 gldx::kMaxBonePalette, static_cast<double>(model.bindDeviation()));
    // The loader keeps unreferenced map paths for diagnostics; RiggedModel folds
    // the four role flags into the same four booleans the HUD reads, so this line
    // and the panel cannot disagree about what the file carried.
    std::fprintf(stderr,
                 "[anim-rigid] textures: albedo %s, normal %s, metallic %s, roughness %s "
                 "(up to %dx%d px); %zu unreferenced map path(s)\n",
                 model.hasAlbedo() ? "yes" : "no", model.hasNormal() ? "yes" : "no",
                 model.hasMetallic() ? "yes" : "no", model.hasRoughness() ? "yes" : "no",
                 model.textureWidth(), model.textureHeight(),
                 model.missingTexturePaths().size());

    if (reportGait) {
        // Numbers, not vibes: the two things a hand-authored walk gets wrong are
        // (a) the swing foot not clearing the floor and (b) the planted foot
        // drifting up off it. Both are measurable here without a single pixel.
        std::fprintf(stderr, "[anim-rigid] gait: phase  hipL   kneeL  ankleL | soleL    soleR\n");
        std::vector<glm::mat4> pose;
        std::vector<glm::mat4> palette;
        constexpr int kSamples = 16;
        for (int i = 0; i < kSamples; ++i) {
            const float phase = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kSamples);
            // Sample the engine's walk source, not a local copy of the curve: the
            // table is a regression check on what actually ships.
            if (character.walkSource) {
                character.walkSource->Sample(
                    character.walkTimeFor(phase), pose);
            } else {
                gldx::ResetToRest(skeleton, pose);
            }
            skeleton.ComputeSkinning(pose, model.Mat4(), palette);
            float sole[2] = {0.0f, 0.0f};
            for (int side = 0; side < 2; ++side) {
                float lowest = 1.0e9f;
                for (int c = 0; c < character.ground.contactCount(); ++c) {
                    const int bone = character.ground.contactBone(c);
                    if (bone != character.rig.foot[side] && bone != character.rig.toe[side]) {
                        continue;
                    }
                    const glm::vec4 p = palette[static_cast<std::size_t>(bone)] *
                                        glm::vec4(character.ground.contactPoint(c), 1.0f);
                    lowest = std::min(lowest, p.y);
                }
                sole[side] = lowest;
            }
            const float p = static_cast<float>(i) / static_cast<float>(kSamples);
            const LegAngles left = LegAt(character.tune, p);
            std::fprintf(stderr,
                         "[anim-rigid] gait: %5.3f %+6.3f %+6.3f %+6.3f | %+7.4f %+7.4f\n",
                         static_cast<double>(p), static_cast<double>(left.hip),
                         static_cast<double>(left.knee), static_cast<double>(left.ankle),
                         static_cast<double>(sole[0]), static_cast<double>(sole[1]));
        }
    }

    // Root/parent sanity, on stderr, because a flat skeleton is invisible in the
    // bind pose and only shows up as "the feet do not follow the legs".
    {
        int roots = 0;
        int deepest = 0;
        for (std::size_t i = 0; i < skeleton.size(); ++i) {
            const gldx::Bone* bone = skeleton.bone(static_cast<int>(i));
            if (!bone) continue;
            if (bone->parent < 0) ++roots;
            int depth = 0;
            for (int b = bone->parent; b >= 0; b = skeleton.bone(b)->parent) ++depth;
            deepest = std::max(deepest, depth);
        }
        std::fprintf(stderr, "[anim-rigid] hierarchy: %d root bone(s), depth %d, "
                             "foot chain %d -> %d -> %d\n",
                     roots, deepest, character.rig.legUp[gldx::kRigLeft],
                     character.rig.legLow[gldx::kRigLeft], character.rig.foot[gldx::kRigLeft]);

        // Per-action pose check. Numbers, not pictures: this is what lets a caller (or
        // an agent that cannot look at a screenshot) verify that an action is
        // anatomically plausible. Every column is measured on the FINAL pose - after
        // the vertical shift the frame loop applies - because that is what the camera
        // sees. Measuring before the solve is how the previous round shipped a "crouch"
        // that the table put at 0.999 m while the picture stood at 1.208 m: the table
        // was reporting a pose no pixel ever showed, and three tuning rounds chased it.
        //
        //   hip     - pelvis height. A crouch must be clearly LOWER than idle; a value
        //             near -1 m means the legs folded the wrong way and the solve
        //             dragged the body underground.
        //   sole    - lowest sole point. 0 = planted. A jump's samples must leave 0
        //             during the flight, or the character is not leaving the ground.
        //   head    - head bone height: how tall the character reads on screen.
        //   fly     - metres of the shift that came from the source's own arc.
        //   fwd     - pelvis offset from the ankles along the character's forward axis.
        //             This decides whether a crouch reads as crouching (pelvis behind
        //             or over the feet) or as pitching forward over the toes.
        //   foot    - horizontal distance, left ankle to right ankle along forward.
        //             THIS is the column that says "stride". For any action whose
        //             walking cycle is the demo's (walk / crouchwalk) it must equal
        //             `cycleMetres / 2`, because the floor scrolls one whole cycle per
        //             step pair; a foot that spans less skates.
        struct Probe { const char* name; Action action; const gldx::AnimPoseSource* src; };
        const Probe probes[] = {
            {"idle", Action::Idle, character.idleSource.get()},
            {"walk", Action::Walk, character.walkSource.get()},
            {"run", Action::Run, character.runSource.get()},
            {"crouch", Action::Crouch, character.crouchSource.get()},
            {"crouchwalk", Action::CrouchWalk, character.crouchWalkSource.get()},
            {"attack", Action::Attack, character.attackSource.get()},
            {"jump", Action::Jump, character.jumpSource.get()},
            {"sit", Action::Sit, character.sitSource.get()},
            {"swim", Action::Swim, character.swimSource.get()},
        };
        // `GLDX_ARMCHK=1`: how far each wrist sits from the body's centre line.
        //
        // "The arms are inside the skirt" is a claim about a distance, and the
        // silhouette cannot separate "arm tucked against the torso" from "arm hidden
        // behind the hem" - both read as no arm. This prints what the picture cannot.
        if (std::getenv("GLDX_ARMCHK")) {
            for (const Probe& pr : probes) {
                std::vector<glm::mat4> pose, palette;
                pr.src->Sample(0.0f, pose);
                skeleton.ComputeSkinning(pose, model.Mat4(), palette);
                auto pos = [&](int b) {
                    return b < 0 ? glm::vec3(0.0f) : glm::vec3(palette[(std::size_t)b][3]);
                };
                const glm::vec3 hips = pos(character.rig.hips);
                // Already METRES: `palette` comes out of ComputeSkinning with Mat4(),
                // which folds unitScale in. Multiplying by unitScale again - which an
                // earlier version did - reports 0.13 m for a hand that is 1.3 cm from
                // the centre line, i.e. it turns "the arm is inside the body" into
                // "the arm is fine".
                if (&pr == &probes[0])
                    std::fprintf(stderr, "[armchk] unitScale=%.4f hipsWorld=(%.2f,%.2f,%.2f) "
                                         "handWorld=(%.2f,%.2f,%.2f)\n",
                                 (double)model.unitScale(), (double)hips.x, (double)hips.y,
                                 (double)hips.z,
                                 (double)pos(character.rig.hand[gldx::kRigLeft]).x,
                                 (double)pos(character.rig.hand[gldx::kRigLeft]).y,
                                 (double)pos(character.rig.hand[gldx::kRigLeft]).z);
                std::fprintf(stderr, "[armchk] %-11s handL.side=%+.3fm handR.side=%+.3fm\n",
                             pr.name,
                             // `palette` is in RIG units (Mat4's scale does not reach
                             // the bone positions), so convert once here rather than
                             // leave a number labelled "m" that is 10x too big.
                             (double)(model.unitScale() *
                                      glm::dot(pos(character.rig.hand[gldx::kRigLeft]) - hips,
                                               character.rig.side)),
                             (double)(model.unitScale() *
                                      glm::dot(pos(character.rig.hand[gldx::kRigRight]) - hips,
                                               character.rig.side)));
            }
        }
        std::fprintf(stderr, "[anim-rigid] pose check: %-11s %8s %8s %8s %7s %8s %8s %8s\n",
                     "action", "hip", "sole", "head", "fly", "fwd", "foot", "stride");
        for (const Probe& pr : probes) {
            if (pr.src == nullptr) continue;
            float hipSum = 0.0f, soleMin = 1e9f, soleMax = -1e9f, headMax = -1e9f;
            float flyMax = 0.0f, fwdMax = -1e9f, fwdMin = 1e9f, footMax = -1e9f;
            float lowestY = 1e9f;
            const int kSamples = 16;
            for (int i = 0; i < kSamples; ++i) {
                const float t = pr.src->Duration() * static_cast<float>(i)
                                / static_cast<float>(kSamples);
                std::vector<glm::mat4> pose;
                pr.src->Sample(t, pose);
                std::vector<glm::mat4> palette;
                skeleton.ComputeSkinning(pose, model.Mat4(), palette);
                const auto solved = character.ground.Solve(palette);
                const float solvedLift = solved.valid() ? solved.lift : 0.0f;
                const float shift = VerticalShiftFor(character, pr.action, t, solvedLift);
                if (solved.valid()) {
                    gldx::GroundContact::ApplyLift(palette, shift);
                }
                // Bone positions through the SAME palette the mesh is drawn with, so
                // the vertical shift lands on these numbers too.
                // palette[i] * (0,0,0,1) is NOT the bone position: palette is
                // world * inverseBind, so it maps a BIND-space vertex forward. The
                // bone's own posed position is the inverse-bind origin transformed by
                // that same matrix, i.e. palette[i] * (-inverseBind[i] * origin),
                // which is world[i][3] exactly. Taking the origin instead puts every
                // joint at the root's translation and makes the whole table read zero
                // (measured before this fix: idle head 0.014 m).
                // World matrices for this pose. `ComputeWorld` is the one place that
                // knows how a local pose chain composes with the root transform, and it
                // already applies the model's unit scale - which is why neither the
                // palette (mesh->bone) nor `RestWorld * pose` (double transform) can be
                // used as a drop-in for a joint's world position.
                std::vector<glm::mat4> worldMats;
                skeleton.ComputeWorld(pose, model.Mat4(), worldMats);
                auto bonePos = [&](int bone) {
                    // `bone < 0` is the normal "this rig has no such slot" case, not an
                    // error: skeleton.bone(-1) is nullptr, and dereferencing it crashed
                    // with EXC_BAD_ACCESS at 0x4c on a rig with unbound slots.
                    if (bone < 0 || bone >= static_cast<int>(worldMats.size())) return glm::vec3(0.0f);
                    return glm::vec3(worldMats[(std::size_t)bone][3]);
                };
                const glm::vec3 hip = bonePos(character.rig.hips);
                const glm::vec3 head = bonePos(character.rig.head);
                const glm::vec3 ankleL = bonePos(character.rig.foot[gldx::kRigLeft]);
                const glm::vec3 ankleR = bonePos(character.rig.foot[gldx::kRigRight]);
                // Grounded solves are allowed the tolerance; a flight sample is the
                // one that matters ("this frame really is off the floor").
                const float sole = -solvedLift + shift;
                // Lowest point over EVERY bone, not just the four sole contacts.
                //
                // `sole` only knows the feet, so a pose that buries a knee, a hip or a
                // hand reports sole = 0 while visibly sinking through the floor (the sit
                // pose did exactly that). This walks the whole palette so "does anything
                // go below y = 0" is answerable for any action.
                // Floor-violation probe, using only quantities this table already
                // trusts: `sole` (the solved sole height) and `hip`.
                //
                // A bone sweep is the obvious idea and it does not work here: bone
                // origins sit at the joints, so a kneeling/sitting pose has knee and
                // hip origins a metre below the hips while the SKIN is on the floor.
                // Measured, that reported every authored-height pose as sinking by
                // over a metre - the number was the knee joint, not a violation.
                //
                // What can be checked without the mesh:
                //   ground-solved poses -> `sole` is the real floor distance, must be >= 0
                //   authored-height poses -> the hips must be a plausible height above
                //   the floor (not negative, not standing)
                lowestY = std::min(lowestY, sole);
                hipSum += hip.y;
                soleMin = std::min(soleMin, sole);
                soleMax = std::max(soleMax, sole);
                headMax = std::max(headMax, head.y);
                if (pr.action == Action::Jump && character.jumpSource) {
                    flyMax = std::max(flyMax, character.jumpSource->AirborneRiseAt(t));
                }
                const float fwd = glm::dot(hip - 0.5f * (ankleL + ankleR), character.rig.forward);
                fwdMax = std::max(fwdMax, fwd);
                fwdMin = std::min(fwdMin, fwd);
                footMax = std::max(footMax, glm::dot(ankleL - ankleR, character.rig.forward));
            }
            const float hipAvg = hipSum / static_cast<float>(kSamples);
            // Two hard rules the whole action set must satisfy:
            //   SINK   - something ends up below the floor (y < -2 cm)
            //   FLOAT  - nothing touches the floor at all (lowest > +6 cm) for a pose
            //            that is supposed to be in contact
            // Airborne poses are exempt: run and jump spend part of the cycle with
            // NOTHING near the floor, which is correct, and their ground samples are
            // already validated by the `sole` and `fly` columns.
            const bool airborne = pr.action == Action::Jump || pr.action == Action::Run;
            const bool sinks  = !airborne && lowestY < -0.03f;
            const bool floats = !airborne && (lowestY > 0.10f) && pr.action != Action::Swim;
            const char* verdict = sinks ? "  <- SINKS" : (floats ? "  <- FLOATS" : "");
            if (std::getenv("GLDX_LOW")) {
                std::fprintf(stderr, "  [low] %s lowestY=%.3f\n", pr.name, (double)lowestY);
            }
            // The stride a walking cycle implies: the floor scrolls cycleMetres per
            // cycle, i.e. one step pair. Anything else is a skate, and the number is
            // reported next to the measured foot span so the two can be compared
            // instead of guessed at.
            const float strideScale = (pr.action == Action::CrouchWalk && character.crouchWalkSource)
                                          ? character.crouchWalkSource->strideScale() : 1.0f;
            const float expected = (pr.action == Action::Walk || pr.action == Action::CrouchWalk)
                                       ? 0.5f * character.cycleMetres * strideScale
                                       : 0.0f;
            std::fprintf(stderr,
                         "[anim-rigid] pose check: %-11s %7.3fm %7.3fm %7.3fm %6.2fm "
                         "%+7.3fm %7.3fm %7.3fm%s\n",
                         pr.name, static_cast<double>(hipAvg),
                         static_cast<double>(0.5f * (soleMin + soleMax)),
                         static_cast<double>(headMax), static_cast<double>(flyMax),
                         static_cast<double>(0.5f * (fwdMin + fwdMax)),
                         static_cast<double>(footMax), static_cast<double>(expected),
                         (expected > 0.0f && footMax < 0.75f * expected) ? "  <- SKATES"
                                                                        : verdict);
        }
    }

    // Per-time trace of ONE action, 16 rows. Coarser or finer than the table above:
    // that one samples one point per action, this one walks the timeline, which is
    // the only way to see a transient - a one-frame pop between two poses does not
    // appear in a table of eight evenly spaced samples, and it is exactly what a
    // freeze-frame grid also hides (each cell is a separate still).
    //
    //   air     - the source's own claim about flight height at t (jump). If this is
    //             non-zero and `sole` is not, the arc is not reaching the picture.
    //   sole    - lowest sole after the frame loop's shift. Must be 0 while grounded
    //             and positive exactly where `air > 0`.
    //   head    - head bone height, i.e. what the silhouette does.
    //   kneeFwd - knee bone ahead of the hip along forward, in metres. A positive
    //             number during STANCE is the "leg sticking out in front" failure: the
    //             swinging knee should be the one that goes forward, and a planted leg
    //             should have its knee behind or under the hip.
    if (traceAction != nullptr && *traceAction != '\0') {
        Action action = Action::Walk;
        const gldx::AnimPoseSource* src = character.walkSource.get();
        if (std::strcmp(traceAction, "idle") == 0) { action = Action::Idle; src = character.idleSource.get(); }
        else if (std::strcmp(traceAction, "run") == 0) { action = Action::Run; src = character.runSource.get(); }
        else if (std::strcmp(traceAction, "crouch") == 0) { action = Action::Crouch; src = character.crouchSource.get(); }
        else if (std::strcmp(traceAction, "crouchwalk") == 0) { action = Action::CrouchWalk; src = character.crouchWalkSource.get(); }
        else if (std::strcmp(traceAction, "attack") == 0) { action = Action::Attack; src = character.attackSource.get(); }
        else if (std::strcmp(traceAction, "jump") == 0) { action = Action::Jump; src = character.jumpSource.get(); }
        else if (std::strcmp(traceAction, "sit") == 0) { action = Action::Sit; src = character.sitSource.get(); }
        else if (std::strcmp(traceAction, "swim") == 0) { action = Action::Swim; src = character.swimSource.get(); }
        if (src != nullptr) {
            // The trace's rows are the source's OWN timeline (t = 0 .. Duration). For an
            // action the frame loop samples at the raw clock that is the same thing, but
            // for the phase-driven ones (walk / crouchwalk) the demo maps the clock
            // through `walkTimeFor`, so a row here is a phase, not a wall-clock instant.
            // Say which, so `--on trace` + `--action X --freeze-at Y` cannot be read as
            // "Y seconds into the clip" when it is not.
            const bool phaseDriven = (action == Action::Walk || action == Action::CrouchWalk);
            std::fprintf(stderr, "[anim-rigid] trace: %s over %.3f s%s\n", src->name(),
                         static_cast<double>(src->Duration()),
                         phaseDriven ? "  (phase-driven: feed the source t = walkTimeFor(phase))"
                                     : "  (time-driven: sample at the raw clock)");
            std::fprintf(stderr,
                         "[anim-rigid] trace: %6s %6s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s %8s\n",
                         "t", "phase", "air", "shift", "sole", "head", "hipH",
                         "ankleH", "kneeFwd", "hipFwd", "handFwd", "handUp", "handLen",
                         "wFwd", "wSide", "armLf", "armRf");
            constexpr int kRows = 16;
            for (int i = 0; i < kRows; ++i) {
                const float t = src->Duration() * static_cast<float>(i)
                                / static_cast<float>(kRows);
                std::vector<glm::mat4> pose;
                src->Sample(t, pose);
                std::vector<glm::mat4> palette;
                skeleton.ComputeSkinning(pose, model.Mat4(), palette);
                const auto solved = character.ground.Solve(palette);
                const float solvedLift = solved.valid() ? solved.lift : 0.0f;
                const float shift = VerticalShiftFor(character, action, t, solvedLift);
                if (solved.valid()) gldx::GroundContact::ApplyLift(palette, shift);
                // Same convention as the table above: a bone's posed position is
                // palette * (its inverse-bind origin), i.e. world[3], not palette * 0.
                auto bonePos = [&](int b) {
                    if (b < 0 || b >= static_cast<int>(palette.size())) return glm::vec3(0.0f);
                    const glm::vec4 p = palette[(std::size_t)b] *
                                        glm::vec4(-glm::vec3(skeleton.bone(b)->inverseBind[3]),
                                                  1.0f);
                    return glm::vec3(p);
                };
                const glm::vec3 hip = bonePos(character.rig.hips);
                const glm::vec3 head = bonePos(character.rig.head);
                const float air = (action == Action::Jump && character.jumpSource)
                                      ? character.jumpSource->AirborneRiseAt(t) : 0.0f;
                // The columns that decide whether a crouch reads as a crouch: where the
                // seat is (hip height), where the knees are (knee forward of the hip:
                // positive = knees in front, which is what a crouch looks like), and
                // whether the pelvis is over the feet or out behind them (hipFwd).
                // heelH: the LOWEST bone of the whole rig (except the root's children
                // below the hips, which is just the feet). For a seated pose this is the
                // pelvis, and it is the number that decides whether the character is
                // actually sitting on the floor or hanging in the air - the ground solve
                // only looks at four SOLE points, so it cannot answer that.
                float heelH = 1e9f;
                for (std::size_t b = 0; b < palette.size(); ++b) {
                    const glm::vec4 pb = palette[b] *
                                         glm::vec4(-glm::vec3(skeleton.bone((int)b)->inverseBind[3]), 1.0f);
                    heelH = std::min(heelH, pb.y);
                }
                const glm::vec3 ankleL = bonePos(character.rig.foot[gldx::kRigLeft]);
                // Hand travel: where the RIGHT wrist ended up relative to its shoulder,
                // projected on the character's own forward/up axes, plus how far it is
                // from the shoulder. An attack that "does nothing" shows up here as a
                // wrist that never moves and a shoulder distance that never grows.
                const glm::vec3 shoulderR = bonePos(character.rig.armUp[gldx::kRigRight]);
                const glm::vec3 wristL = bonePos(character.rig.hand[gldx::kRigLeft]);
                const glm::vec3 wristR = bonePos(character.rig.hand[gldx::kRigRight]);
                const glm::vec3 handVec = wristR - shoulderR;
                const float handFwd = glm::dot(handVec, character.rig.forward);
                const float handUp  = glm::dot(handVec, character.rig.up);
                const float handLen = glm::length(handVec);
                // How far apart the two wrists are ALONG the body (forward) vs ACROSS it
                // (side). A freestyle stroke puts one hand well ahead of the other, so
                // `spreadFwd` must be large; arms fanned out sideways - the failure this
                // column exists to catch - shows up as `spreadSide` large and
                // `spreadFwd` ~0.
                const glm::vec3 dWrist = wristR - wristL;
                (void)dWrist;
                const glm::vec3 kneeL = bonePos(character.rig.legLow[gldx::kRigLeft]);
                const glm::vec3 footMid = 0.5f * (ankleL + bonePos(character.rig.foot[gldx::kRigRight]));
                // ---- joint probe (GLDX_DEBUG_JOINTS=1) ----
                //
                // Prints the WORLD position of the leg joints in the body frame:
                // (fwd, side, y). Three numbers per joint are what makes a sit pose
                // answerable instead of guessable:
                //
                //   knee.side must be well away from 0   (knees out to the sides -> W)
                //   ankle.side must be OUTSIDE knee.side  (shin folds back outward)
                //   both y near 0                         (they lie on the floor)
                //
                // The full-body screenshots hide the legs behind the skirt and the top
                // view cannot show depth, so "which axis is wrong" is unanswerable from
                // pictures alone. This is the instrument for that.
                if (std::getenv("GLDX_DEBUG_JOINTS") != nullptr) {
                    // Measured from the LEFT THIGH ROOT, not from the `hips` bone:
                    // `hips` is the pelvis and its origin can sit well away from where
                    // the legs branch, which made `side` read past the leg's own length.
                    const glm::vec3 rootPos = bonePos(character.rig.legUp[gldx::kRigLeft]);
                    auto jp = [&](int bone) {
                        const glm::vec3 p = bonePos(bone);
                        const glm::vec3 d = p - rootPos;
                        return glm::vec3(glm::dot(d, character.rig.forward),
                                         glm::dot(d, character.rig.side), p.y);
                    };
                    const glm::vec3 hipP = jp(character.rig.legUp[gldx::kRigLeft]);
                    std::fprintf(stderr,
                                 "[joints] %-11s t=%.2f\n"
                                 "  hip   fwd=%+.3f side=%+.3f y=%+.3f\n"
                                 "  kneeL fwd=%+.3f side=%+.3f y=%+.3f\n"
                                 "  ankleL fwd=%+.3f side=%+.3f y=%+.3f\n"
                                 "  kneeR fwd=%+.3f side=%+.3f y=%+.3f\n"
                                 "  ankleR fwd=%+.3f side=%+.3f y=%+.3f\n",
                                 src->name(), static_cast<double>(t),
                                 static_cast<double>(hipP.x), static_cast<double>(hipP.y),
                                 static_cast<double>(hipP.z),
                                 static_cast<double>(jp(character.rig.legLow[gldx::kRigLeft]).x),
                                 static_cast<double>(jp(character.rig.legLow[gldx::kRigLeft]).y),
                                 static_cast<double>(jp(character.rig.legLow[gldx::kRigLeft]).z),
                                 static_cast<double>(jp(character.rig.foot[gldx::kRigLeft]).x),
                                 static_cast<double>(jp(character.rig.foot[gldx::kRigLeft]).y),
                                 static_cast<double>(jp(character.rig.foot[gldx::kRigLeft]).z),
                                 static_cast<double>(jp(character.rig.legLow[gldx::kRigRight]).x),
                                 static_cast<double>(jp(character.rig.legLow[gldx::kRigRight]).y),
                                 static_cast<double>(jp(character.rig.legLow[gldx::kRigRight]).z),
                                 static_cast<double>(jp(character.rig.foot[gldx::kRigRight]).x),
                                 static_cast<double>(jp(character.rig.foot[gldx::kRigRight]).y),
                                 static_cast<double>(jp(character.rig.foot[gldx::kRigRight]).z));
                }
                std::fprintf(stderr,
                             "[anim-rigid] trace: %6.3f %6.3f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f %8.4f\n",
                             static_cast<double>(t),
                             static_cast<double>(src->Duration() > 0.0f ? t / src->Duration() : 0.0f),
                             static_cast<double>(air), static_cast<double>(shift),
                             static_cast<double>(-solvedLift + shift),
                             static_cast<double>(head.y), static_cast<double>(hip.y),
                             static_cast<double>(ankleL.y),
                             static_cast<double>(glm::dot(kneeL - hip, character.rig.forward)),
                             static_cast<double>(glm::dot(hip - footMid, character.rig.forward)),
                             // Where the head is, along the walking direction: the one
                             // column that says "the torso leans over the feet" instead
                             // of "the character is sitting back on its heels".
                             static_cast<double>(handFwd), static_cast<double>(handUp),
                             static_cast<double>(handLen),
                             static_cast<double>(glm::dot(wristR - wristL, character.rig.forward)),
                             static_cast<double>(glm::abs(glm::dot(wristR - wristL, character.rig.side))),
                             // Upper-arm direction along the body's forward axis, per
                             // side: a freestyle stroke alternates these between ~+1
                             // (pointing at the direction of travel) and ~-1 (pulled
                             // back past the hip). Both of them stuck near 0 means the
                             // arms are fanned out sideways - the failure this column
                             // exists to catch.
                             // Wrist position expressed in the body frame, in metres:
                             // (forward, side). A freestyle stroke traces a large circle
                             // in this plane, so these two columns must sweep through
                             // roughly +/-0.5 m. Values that stay near a constant mean
                             // the arms are pinned - which is what "wiper" looks like.
                             // Body-frame wrist offsets. `bonePos` is WORLD space and the
                             // swim pose rotates the whole body about `orientation()*side`,
                             // so the rig's axes must be rotated by the same matrix before
                             // dotting - otherwise the two columns mix "prone" and
                             // "upright" frames and describe nothing (measured: a wrist
                             // 1.4 m sideways when the arm is 0.52 m long).
                             static_cast<double>(glm::dot(
                                 bonePos(character.rig.hand[gldx::kRigLeft]) -
                                 bonePos(character.rig.hips),
                                 model.orientation() * character.rig.forward)),
                             static_cast<double>(glm::dot(
                                 bonePos(character.rig.hand[gldx::kRigLeft]) -
                                 bonePos(character.rig.hips),
                                 model.orientation() * character.rig.side)));
            }
        }
    }

    // ---- "crouch IS a frame of crouch-walk": verified, not asserted ----
    //
    // The requirement is that switching to crouch must not change the pose at all - a
    // crouch is not a second set of leg constants, it is crouch-walk's curve held at one
    // phase (see ProceduralCrouch.h). So the two sources are sampled at the same instant
    // and every bone matrix is compared. At t = 0 the breathing term is exactly zero
    // (sin(0)), so the answer must be bit-identical; anywhere else it is the breath,
    // which is what `drift` reports in rad-equivalent terms.
    if (character.crouchSource && character.crouchWalkSource) {
        std::vector<glm::mat4> a, b;
        float worst = 0.0f;
        float worstT = 0.0f;
        constexpr int kChecks = 8;
        for (int i = 0; i < kChecks; ++i) {
            const float t = character.crouchSource->Duration() * static_cast<float>(i)
                            / static_cast<float>(kChecks);
            character.crouchSource->Sample(t, a);
            character.crouchWalkSource->SampleAtPhase(character.crouchSource->PosePhase(), b);
            float d = 0.0f;
            for (std::size_t k = 0; k < a.size() && k < b.size(); ++k) {
                const glm::mat4 m = a[k] - b[k];
                for (int c = 0; c < 4; ++c) {
                    d = std::max(d, glm::length(glm::vec3(m[c])));
                }
            }
            if (d > worst) { worst = d; worstT = t; }
        }
        // t = 0 is the exact one; the rest is the breathing overlay and must stay small.
        character.crouchSource->Sample(0.0f, a);
        character.crouchWalkSource->SampleAtPhase(character.crouchSource->PosePhase(), b);
        float exact = 0.0f;
        for (std::size_t k = 0; k < a.size() && k < b.size(); ++k) {
            const glm::mat4 m = a[k] - b[k];
            for (int c = 0; c < 4; ++c) exact = std::max(exact, glm::length(glm::vec3(m[c])));
        }
        // The ground solve runs at 8-bit-visible precision, so this only holds if the
        // two palettes agree to the last bit; report it at full precision rather than
        // rounding to %.3e, which prints 0.000e+00 for anything under the print width.
        // ...and the same comparison through the whole remaining pipeline (skinning
        // matrices + the ground solve), because that - not the local pose - is what the
        // vertex shader reads. A bitmap diff between the two actions cannot be tighter
        // than this number.
        std::vector<glm::mat4> palA, palB;
        skeleton.ComputeSkinning(a, model.Mat4(), palA);
        skeleton.ComputeSkinning(b, model.Mat4(), palB);
        const auto solA = character.ground.Solve(palA);
        const auto solB = character.ground.Solve(palB);
        if (solA.valid()) gldx::GroundContact::ApplyLift(palA, solA.lift);
        if (solB.valid()) gldx::GroundContact::ApplyLift(palB, solB.lift);
        float palDelta = 0.0f;
        for (std::size_t k = 0; k < palA.size() && k < palB.size(); ++k) {
            const glm::mat4 m = palA[k] - palB[k];
            for (int c = 0; c < 4; ++c) palDelta = std::max(palDelta, glm::length(glm::vec3(m[c])));
        }
        std::fprintf(stderr,
                     "[anim-rigid] crouch == crouchwalk@phase %.2f: t=0 pose %.6e, drawn palette "
                     "%.6e (both must be 0); worst over a breath %.3e at t=%.2f s\n",
                     static_cast<double>(character.crouchSource->PosePhase()),
                     static_cast<double>(exact), static_cast<double>(palDelta),
                     static_cast<double>(worst), static_cast<double>(worstT));
    }

    // Which rig slots failed to bind, and the one assertion this demo takes most
    // seriously: RigProfile keeps a missing bone as -1 and every rotation against
    // it is quietly a no-op, so a slot that failed to bind is invisible in the
    // picture (the limb just looks stiff) until this line names it. It used to be
    // hand-written here and lived through several review rounds with hand and
    // clavicle silently unbound.
    std::fprintf(stderr, "[anim-rigid] rig: %s (naming family: %s)\n",
                 character.rig.BindingReport().c_str(), character.rig.namingFamily());

    // `GLDX_PROPS=1`: the skeleton's own proportions.
    //
    // Every number in a *Tuning struct is a fraction of one of these - a hip swing of
    // 0.33 rad only means a given stride because of the thigh:calf ratio it was measured
    // on. Copying the angles to a rig with a different ratio is how "the same walk"
    // turns into a skate, or into knees that bend twice as far as the mesh allows.
    // Printing the ratios is what makes "re-derive the defaults for this asset" a
    // measurement instead of a guess.
    if (std::getenv("GLDX_PROPS")) {
        const auto& sk = model.skeleton();
        auto dist = [&](int a, int b) {
            if (a < 0 || b < 0) return 0.0f;
            return glm::length(glm::vec3(sk.RestWorld(a)[3]) - glm::vec3(sk.RestWorld(b)[3])) *
                   model.unitScale();
        };
        const float thighL = dist(character.rig.legUp[gldx::kRigLeft], character.rig.legLow[gldx::kRigLeft]);
        const float shinL  = dist(character.rig.legLow[gldx::kRigLeft], character.rig.foot[gldx::kRigLeft]);
        const float upperA = dist(character.rig.armUp[gldx::kRigLeft], character.rig.armLow[gldx::kRigLeft]);
        const float foreA  = dist(character.rig.armLow[gldx::kRigLeft], character.rig.hand[gldx::kRigLeft]);
        const float shoulderW = dist(character.rig.clavicle[gldx::kRigLeft], character.rig.clavicle[gldx::kRigRight]);
        const float hipW = 2.0f * std::abs(glm::dot(
            glm::vec3(sk.RestWorld(character.rig.legUp[gldx::kRigLeft])[3]) -
                glm::vec3(sk.RestWorld(character.rig.hips)[3]), character.rig.side));
        std::fprintf(stderr,
                     "[props] height=%.3fm leg=%.3fm thigh=%.3f calf=%.3f (calf/thigh=%.3f) "
                     "upperArm=%.3f forearm=%.3f shoulderW=%.3f hipW=%.3f armDown=%.3frad\n",
                     (double)model.heightMetres(), (double)model.legLengthMetres(),
                     (double)thighL, (double)shinL,
                     (double)(thighL > 1e-4f ? shinL / thighL : 0.0f),
                     (double)upperA, (double)foreA, (double)shoulderW, (double)hipW,
                     (double)character.tune.armDown);
    }
}

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {"hud", "walk", "ground", "wireframe", "maps", "normalmap", "report", "trace", "allparts"},
                                 {"model", "speed", "fov", "yaw", "pitch", "distance", "freeze-at",
                                  "target-y", "action", "naming", "hide-mat", "only-mat"},
                                 "anim-rigid");
    if (flags.wantsHelp()) {
        flags.printUsage();
        return 0;
    }

    View view;
    view.walk      = flags.on("walk", true);
    view.ground    = flags.on("ground", true);
    view.wireframe = flags.on("wireframe", false);
    view.maps      = flags.on("maps", true);
    view.normalMap = flags.on("normalmap", true);
    view.showHud   = flags.on("hud", true);
    view.allParts  = flags.on("allparts", false);
    view.report    = flags.on("report", false);
    view.trace     = flags.on("trace", false);
    view.speed     = flags.real("speed", view.speed, 0.2f, 6.0f);
    view.fov       = flags.real("fov", view.fov, 15.0f, 100.0f);
    view.hideMaterial = flags.integer("hide-mat", -1);
    view.onlyMaterial = flags.integer("only-mat", -1);
    view.orbit.settings().yaw =
        flags.real("yaw", view.orbit.settings().yaw);
    view.orbit.settings().pitch =
        flags.real("pitch", view.orbit.settings().pitch, -1.45f, 1.45f);
    view.orbit.settings().distance =
        flags.real("distance", view.orbit.settings().distance, 0.8f, 30.0f);
    view.freezeAt  = flags.real("freeze-at", 0.0f, 0.0f, 1.0e6f);
    view.orbit.settings().target.y =
        flags.real("target-y", view.orbit.settings().target.y, 0.0f, 3.0f);

    // Which action to play. Exists so the non-walk actions can be verified without
    // a keyboard: the number keys are unreachable from a `--quit-after` run, and a
    // switch that can only be exercised by a human is a switch nobody regression
    // tests. Unknown names are ignored (the demo keeps walking) rather than fatal -
    // this is a demo, and a typo should not stop the window from opening.
    {
        const std::string action = flags.string("action", "walk");

        if (action == "idle")        view.Request(Action::Idle, 0.0);
        else if (action == "walk")   view.Request(Action::Walk, 0.0);
        else if (action == "run")    view.Request(Action::Run, 0.0);
        else if (action == "crouch") view.Request(Action::Crouch, 0.0);
        else if (action == "crouchwalk") view.Request(Action::CrouchWalk, 0.0);
        else if (action == "attack") view.Request(Action::Attack, 0.0);
        else if (action == "jump")   view.Request(Action::Jump, 0.0);
        else if (action == "sit")    view.Request(Action::Sit, 0.0);
        else if (action == "swim")   view.Request(Action::Swim, 0.0);
        else {
            std::fprintf(stderr, "[anim-rigid] unknown --action '%s'; using walk "
                                 "(known: idle, walk, run, crouch, crouchwalk, attack, jump, sit, swim)\n",
                         action.c_str());
        }
    }
    const std::string modelPath = flags.string("model", kDefaultModel);

    gldx::win::WindowDesc desc;
    desc.width  = 1280;
    desc.height = 760;
    desc.title  = "gldx demo - anim-rigid (skinned FBX + procedural walk)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();

    Character character;

    // Stage A on a worker thread; the future is drained inside the frame loop so
    // a 30 MB parse never stalls the first frames (the window is already up).
    std::future<std::expected<gldx::LoadedSkinnedModelData, std::string>> load =
        std::async(std::launch::async, [modelPath] {
            // Parse through the on-disk cache. A 12 MB FBX with 800k vertices spends
            // 55 s inside assimp's FBX reader, and that cost is paid EVERY launch;
            // with the cache a second run reads our own structure instead. The cache
            // keys on the file's size+mtime and a format version, so editing the model
            // (or the layout) silently re-parses rather than serving stale data.
            gldx::ModelCacheOptions cache;
            const char* noCache = std::getenv("GLDX_NO_MODEL_CACHE");
            cache.enabled = (noCache == nullptr || *noCache == '\0');
            auto parsed = gldx::LoadSkinnedThroughCache(
                modelPath, cache,
                [modelPath] { return gldx::ModelLoader::LoadSkinned(modelPath); });
            // ---- a texture-less export borrows its maps from the original ----
            //
            // A Blender FBX export of an MMD model routinely drops every material map:
            // the .fbx and the .pmx here are the same 240-bone / 60-mesh / 800763-vertex
            // character, but the FBX has one empty material and `albedo no`, while the
            // PMX carries all 15 atlases. The meshes are in the SAME order with the SAME
            // vertex counts, so the material table can be transplanted mesh-for-mesh.
            //
            // This is a demo-side convenience, deliberately not a loader feature: the
            // loader's job is to report what the file contains, and "silently borrow a
            // sibling file's textures" is a policy this particular asset pair justifies
            // and a general one does not. It only fires when the parse came back with
            // NO albedo anywhere, so a self-contained asset is never touched.
            if (parsed && !parsed->meshes.empty()) {
                const bool anyMap = std::any_of(parsed->materials.begin(), parsed->materials.end(),
                                                [](const gldx::SkinnedMaterial& m) {
                                                    return m.albedoTexture >= 0;
                                                });
                if (!anyMap) {
                    // Candidates: first the same stem (`name.fbx` -> `name.pmx`), then
                    // every other `.pmx` in the same directory. The stem match alone is
                    // not enough here - this asset's FBX is called `未命名.fbx` while the
                    // PMX it was exported from is `Lamy_Idol_Mark2_1.0.pmx`, so the pair
                    // shares a directory but not a name. What actually identifies "same
                    // character" is the mesh sequence, and that is verified below before
                    // anything is taken.
                    std::vector<std::string> candidates;
                    const std::size_t slash = modelPath.find_last_of('/');
                    const std::string dir =
                        (slash == std::string::npos) ? std::string() : modelPath.substr(0, slash + 1);
                    const std::size_t dot = modelPath.find_last_of('.');
                    if (dot != std::string::npos && dot > slash) {
                        std::string stem = modelPath;
                        stem.replace(dot, std::string::npos, ".pmx");
                        if (stem != modelPath) candidates.push_back(stem);
                    }
                    std::error_code ec;
                    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                        if (!entry.is_regular_file()) continue;
                        const std::string p = entry.path().string();
                        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".pmx") == 0 &&
                            p != modelPath && std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
                            candidates.push_back(p);
                        }
                    }
                    for (const std::string& sibling : candidates) {
                        auto donor = gldx::ModelLoader::LoadSkinned(sibling);
                        // The transplant is only honest when the two really are the same
                        // mesh sequence; a mismatch here would paint the face with the
                        // socks atlas, which is far worse than staying grey.
                        const bool sameShape =
                            donor && donor->meshes.size() == parsed->meshes.size() &&
                            std::equal(donor->meshes.begin(), donor->meshes.end(),
                                       parsed->meshes.begin(),
                                       [](const gldx::SkinnedMeshData& a,
                                          const gldx::SkinnedMeshData& b) {
                                           return a.vertices.size() == b.vertices.size();
                                       });
                        if (!sameShape) continue;
                        parsed->materials = std::move(donor->materials);
                        parsed->meshMaterial = std::move(donor->meshMaterial);
                        parsed->textures = std::move(donor->textures);
                        std::fprintf(stderr,
                                     "[anim-rigid] %s carries no material maps; borrowed "
                                     "%zu material(s) / %zu texture(s) from %s\n",
                                     modelPath.c_str(), parsed->materials.size(),
                                     parsed->textures.size(), sibling.c_str());
                        break;
                    }
                }
            }
            return parsed;
        });

    auto skinPass = std::make_unique<SkinPass>(character, view);
    auto hudPass  = std::make_unique<WalkHudPass>(character, view);
    WalkHudPass* hudRaw = hudPass.get();
    hudRaw->hud.SetVisible(view.showHud);
    renderer.AddPass(std::move(skinPass));
    renderer.AddPass(std::move(hudPass));

    gldx::Camera camera;
    camera.SetPerspective(view.fov, 1.0f, 0.05f, 200.0f);

    gldx::SpriteBatch sprite;
    if (!sprite.Init()) std::fprintf(stderr, "[anim-rigid] SpriteBatch init failed; no HUD text\n");
    gldx::Font font;
    if (auto loaded = font.LoadSystem(48.0f); !loaded) {
        std::fprintf(stderr, "[anim-rigid] font: %s (HUD will be empty)\n", loaded.error().c_str());
    }
    gldx::Texture2D white;
    white.Upload(MakeSolidDesc());

    // The three dummies the character draws with when the file has no such map now
    // live inside RiggedModel (fallbackAlbedo/Normal/Scalar). They are the reason
    // a shader with an unbacked sampler cannot silently drop this draw on Apple.

    // Input goes through the gldxwin surface only (GLFW's user pointer belongs to
    // that library). A drag orbits, the wheel scales the distance.
    window.OnCursor([&view](gldx::win::Window&, gldx::win::Vec2d pos) {
        if (!view.dragging) return;
        view.orbit.Drag(glm::vec2(static_cast<float>(pos.x - view.lastX),
                                  static_cast<float>(pos.y - view.lastY)));
        view.lastX = pos.x;
        view.lastY = pos.y;
    });
    window.OnMouseButton([&view](gldx::win::Window& w, gldx::win::MouseButton button,
                                 gldx::win::KeyAction action, int) {
        if (button != gldx::win::MouseButton::Left) return;
        view.dragging = action == gldx::win::KeyAction::Press;
        const gldx::win::Vec2d c = w.CursorPos();
        view.lastX = c.x;
        view.lastY = c.y;
    });
    window.OnScroll([&view](gldx::win::Window&, double, double dy) {
        // exp(-dy*0.12) and OrbitSettings::zoomFactor=0.9 give the same feel; the
        // clamp (0.8..30 m) lives in the settings so it cannot drift per call site.
        view.orbit.Scroll(static_cast<float>(dy) * 1.2f);
    });
    window.OnKey([&view, hudRaw](gldx::win::Window&, gldx::win::Key key,
                                 gldx::win::KeyAction action, int) {
        if (action == gldx::win::KeyAction::Release) return;
        switch (key) {
            case gldx::win::Key::W: view.walk = !view.walk; break;
            case gldx::win::Key::G: view.ground = !view.ground; break;
            case gldx::win::Key::F: view.wireframe = !view.wireframe; break;
            // Action selection. Only records the request; the frame loop applies it
            // to the machine, because switching actions is a per-frame decision
            // (it needs the current phase) and must not happen inside a GLFW
            // callback that can fire between two of our own updates.
            // One helper for all of them: it records WHEN, so a one-shot action
            // (attack/jump) starts its clock on the key press rather than inheriting
            // "we are 3 s into the demo, which is past the end of the clip".
            // `App::Now()` is the demo's own clock (glfwGetTime is forbidden outside
            // the window library), and it is the same time base the frame loop uses.
            case gldx::win::Key::Num0: view.Request(Action::Idle, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::Num1: view.Request(Action::Walk, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::Num2: view.Request(Action::Run, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::Num3: view.Request(Action::Crouch, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::Num4: view.Request(Action::CrouchWalk, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::Num5: view.Request(Action::Attack, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::Num6: view.Request(Action::Jump, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::Num7: view.Request(Action::Sit, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::Num8: view.Request(Action::Swim, gldx::win::App::Get().Now()); break;
            case gldx::win::Key::H:
                view.showHud = !view.showHud;
                hudRaw->hud.SetVisible(view.showHud);
                break;
            default: break;
        }
    });

    // Which action the `--on trace` table should walk. Resolved once, next to the
    // action parsing, so the table and `--action` cannot disagree.
    const std::string traceAction = view.trace
        ? flags.string("action", "walk") : std::string();

    // Naming channel: which family resolves this asset's bone names. `auto` (default)
    // lets the engine's general-purpose families compete; `mmd` picks the demo-side
    // MMD/PMX family. This is the switch that lets the SAME pose sources drive a
    // Mixamo-named rig and a Japanese-named one - the engine has no idea either exists.
    {
        // A .pmx file is MMD by construction, so auto-detect from the name rather than
        // forcing the user to remember a flag pair. `--naming auto|mmd` still wins.
        const bool looksMmd = modelPath.size() > 4 &&
                              modelPath.compare(modelPath.size() - 4, 4, ".pmx") == 0;
        const std::string namingOpt = flags.string("naming", looksMmd ? "mmd" : "auto");
        character.namingChannel = (namingOpt == "mmd") ? NamingChannel::Mmd : NamingChannel::Auto;
    }

    // Test hook: press a number key at a given wall-clock second, so the behaviour the
    // number keys produce can be reproduced from a script. Without it the only way to
    // exercise "press 5 at t = 5 s" was a human hand, which is how the bug below
    // survived: `--action attack` starts AT t = 0, where the old code was correct.
    //
    //   GLDX_PRESS_AT=5 GLDX_PRESS_KEY=5  -> treat as "the user pressed 5 at 5 s"
    const char* pressAtEnv = std::getenv("GLDX_PRESS_AT");
    const char* pressKeyEnv = std::getenv("GLDX_PRESS_KEY");
    const double pressAt = (pressAtEnv && *pressAtEnv) ? std::atof(pressAtEnv) : -1.0;
    const int pressKey = (pressKeyEnv && *pressKeyEnv) ? std::atoi(pressKeyEnv) : -1;
    bool pressDone = false;
    // A SECOND press, so "tap 6 twice quickly" is scriptable. It exists because that is
    // the one interaction a single hook cannot express, and it is exactly where a
    // one-shot action has to restart instead of being swallowed.
    const char* press2AtEnv = std::getenv("GLDX_PRESS_AT2");
    const char* press2KeyEnv = std::getenv("GLDX_PRESS_KEY2");
    const double press2At = (press2AtEnv && *press2AtEnv) ? std::atof(press2AtEnv) : -1.0;
    const int press2Key = (press2KeyEnv && *press2KeyEnv) ? std::atoi(press2KeyEnv) : -1;
    bool press2Done = false;

    bool firstFrame = true;
    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        // Stage B: the worker finished, so build the GL objects on this thread.
        // The very first frame waits for it instead of drawing an empty stage:
        // the window is already up (that is what the worker is for), but a frame
        // that may or may not contain the character is not reproducible, and this
        // demo is verified by snapshotting a frozen clock. Blocking once here
        // costs the parse time and makes every later frame identical for
        // identical argv.
        if (firstFrame && load.valid()) {
            const auto t0 = std::chrono::steady_clock::now();
            load.wait();
            const auto t1 = std::chrono::steady_clock::now();
            std::fprintf(stderr, "[timing] parse (worker) = %.2f s\n",
                         std::chrono::duration<double>(t1 - t0).count());
            firstFrame = false;
        }
        if (load.valid() && load.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = load.get();
            if (result) {
                UploadCharacter(character, std::move(*result), view.report, traceAction.c_str());
            } else {
                character.error = result.error();
                std::fprintf(stderr, "[anim-rigid] load failed: %s\n", result.error().c_str());
            }
        }

        // The clock, frozen or not. Reading min(time, freezeAt) instead of
        // accumulating dt is what makes --freeze-at snapshots comparable: the
        // pose is a function of the clock, never of the frame rate.
        view.clock = (view.freezeAt > 0.0f) ? std::min(info.time, static_cast<double>(view.freezeAt))
                                            : info.time;
        const float animSeconds = static_cast<float>(view.clock);
        // The floor scrolls at the character's real speed, except while crouch-walking:
        // that gait lays down `strideScale()` of the standing stride per cycle (see the
        // crouchwalk branch below), so the ground must advance by the same factor or
        // the planted foot slides.
        const float scrollScale = (view.requestAction == Action::CrouchWalk
                                   && character.crouchWalkSource)
                                      ? character.crouchWalkSource->strideScale() : 1.0f;
        view.scroll = view.speed * scrollScale * animSeconds;

        if (character.ready()) {
            const gldx::RiggedModel& model = character.model;

            // One-shot actions hand control back to idle when they end.
            //
            // Two rules here, and both were missing:
            //
            //  1. The timers must MEASURE from the moment the action was requested,
            //     not from the demo's own start. `oneShotFinished(t)` takes a clip
            //     time, and this used to pass the absolute clock - so by the time you
            //     had walked around for a second, `t >= Duration()` was already true
            //     and pressing 5 or 6 returned to idle on the very next frame. That
            //     read as "the number keys don't work for attack and jump".
            //  2. Auto-return is switched off under `--freeze-at`: a frozen frame is
            //     meant to sit exactly where the clock put it, and auto-returning
            //     would make attack/jump's final frame impossible to snapshot
            //     (`--action attack --freeze-at 2`).
            view.actionElapsed = animSeconds - view.actionStartedAt;
            if (view.freezeAt <= 0.0f
                && (view.requestAction == Action::Attack || view.requestAction == Action::Jump)
                && character.oneShotFinished(view.requestAction, view.actionElapsed)) {
                view.Request(Action::Idle, animSeconds);
            }

        // Simulated key press (see GLDX_PRESS_AT above). It goes through the same
        // View::Request the OnKey handler uses, so this exercises the real path.
        if (!press2Done && press2At >= 0.0 && animSeconds >= press2At && press2Key >= 0) {
            press2Done = true;
            static const Action kByKey2[9] = {Action::Idle, Action::Walk, Action::Run,
                                             Action::Crouch, Action::CrouchWalk,
                                             Action::Attack, Action::Jump, Action::Sit,
                                             Action::Swim};
            if (press2Key < 9) view.Request(kByKey2[press2Key], animSeconds);
        }
        if (!pressDone && pressAt >= 0.0 && animSeconds >= pressAt && pressKey >= 0) {
            pressDone = true;
            static const Action kByKey[9] = {Action::Idle, Action::Walk, Action::Run,
                                             Action::Crouch, Action::CrouchWalk,
                                             Action::Attack, Action::Jump, Action::Sit,
                                             Action::Swim};
            if (pressKey < 9) view.Request(kByKey[pressKey], animSeconds);
        }
        // The clock every TIME-DRIVEN action is sampled at.
        //
        // It is the clip's own time - `now - when the action was requested` - not the
        // demo's absolute clock. Sampling at the absolute clock was the reason the
        // number keys looked dead for attack and jump: `--action attack` starts at
        // t = 0, where the two clocks coincide, so it always looked right; but press 5
        // five seconds into a session and the clip was sampled at t = 5.0 s, which is
        // past its end, i.e. it clamped to the last frame - the recovery stance. The
        // auto-return timer was measured the same wrong way, so it handed control back
        // to idle on the very next frame. Both are fixed by using one clock.
        const float clipSeconds = view.actionElapsed;

        const float phase = character.phaseFor(view.speed, animSeconds);

        // ---- every action goes through the state machine, so switching is a
        // ---- CROSS-FADE instead of a cut ----
        //
        // The demo used to pick a source and call Sample() on it directly. That is right
        // for a reference still (`--freeze-at` frames must be exactly the source's own
        // pose) but wrong for the interactive path: pressing a number key snapped the
        // character from one pose to a completely different one in a single frame.
        //
        // Now both paths go through `machine`: the same source, the same clock, but the
        // request is a *transition*. Two consequences worth naming:
        //
        //   * The clip clock the machine keeps is what gets sampled, so `clipSeconds` is
        //     no longer the sampling argument - it is what the auto-return rule reads,
        //     and what the machine is fed (one dt per frame).
        //   * The two phase-driven gaits (walk / crouchwalk) are NOT sampled at machine
        //     time: the demo owns the treadmill, so their phase comes from `phase`
        //     above. Both are fed through the machine anyway so that walk -> crouchwalk
        //     and walk -> run blend like everything else.
        // A frame's dt, CLAMPED. The first frame blocks on the model load (seconds), and
        // a machine fed that dt finishes any 0.18 s transition inside a single frame -
        // which is exactly how "the cross-fade does not happen for some switches"
        // appeared: the switch worked, the transition was over before the first frame
        // the new pose could be seen on. 50 ms cap = never let one stalled frame skip a
        // visible transition, still long enough for real frame times.
        const float rawDt = animSeconds - view.lastAnimSeconds;
        const float dt = std::clamp(rawDt, 0.0f, 0.05f);
        view.lastAnimSeconds = animSeconds;

        if (!view.machineInitialised) {
            character.machine.Set(character.sourceFor(view.requestAction));
            view.machineInitialised = true;
        } else {
            gldx::AnimTransition transition;
            transition.seconds = 0.18f;
            transition.atContact = false;

            // Forward the request ONLY when it is new (see View::Request).
            //
            // This is the bug that made the jump "glued to the floor": the request state
            // persists for the whole action, so this used to re-request on EVERY frame.
            // `AnimStateMachine::Request` treats "same source as current" as a no-op -
            // but for a ONE-SHOT action the right reading of a repeat is "start over",
            // and the machine cannot know that. The result was a jump that restarted at
            // t = 0 every frame, i.e. forever frozen in the crouch phase: `--on trace`
            // showed sole rising to 1.13 m (it starts once there) while pressing 6
            // showed no lift at all.
            if (view.requestSerial != view.lastSentSerial) {
                view.lastSentSerial = view.requestSerial;
                const gldx::AnimPoseSource* want = character.sourceFor(view.requestAction);
                // One-shot actions RESTART; looping ones just request.
                //
                // `Request` is a no-op when the source is already current, which is right
                // for a gait (the caller re-requests every frame and must not reset the
                // phase) but wrong for jump/attack: tapping 6 while a jump is mid-air has
                // to play the jump again from its first frame. Without this the second
                // press was swallowed and the character froze part-way through the
                // previous jump.
                const bool oneShot = (view.requestAction == Action::Jump
                                      || view.requestAction == Action::Attack);
                if (oneShot) {
                    character.machine.Restart(want, transition);
                } else {
                    character.machine.Request(want, transition);
                }
            }
            character.machine.Update(dt);
        }



        // Lightweight guard: a null source (rig not uploaded yet) leaves the pose alone.
        if (character.machine.Current() != nullptr) {
            const gldx::AnimPoseSource* cur = character.machine.Current();
            const gldx::AnimPoseSource* nxt = character.machine.Pending();

            // For the phase-driven gaits, drive the machine's own clock from the
            // treadmill phase so the feet stay in step with the floor. This is legal
            // because the machine's time for a looping source IS its phase.
            if (cur == character.walkSource.get() || cur == character.crouchWalkSource.get()) {
                const float stride = (cur == character.crouchWalkSource.get())
                                         ? character.crouchWalkSource->strideScale() : 1.0f;
                character.machine.SetCurrentTime(character.walkTimeFor(phase * stride));
            }

            if (nxt == nullptr) {
                character.machine.Evaluate(character.pose);
            } else {
                // Mid-transition. The machine blends the two sources it was given, and
                // for the gaits that means both at the same phase (a walk-to-run blend
                // that ran at two different phases would scrub). Sample them here
                // instead: drive the machine's two times, then let it blend.
                if (nxt == character.walkSource.get() || nxt == character.crouchWalkSource.get()) {
                    character.machine.SetPendingTime(character.walkTimeFor(phase));
                }
                character.machine.Evaluate(character.pose);
            }

            // Swim is the one action that needs a ROOT rotation, and it cannot live in
            // the pose sources (they write LOCAL matrices; this is a WORLD-axis rotate).
            // It is applied AFTER the blend so a walk -> swim transition tilts the body
            // in gradually - the machine's weight is what makes that smooth, and
            // rotating before the blend would blend a lying body with a standing one.
            // The prone rotation is NOT applied here any more: the swim source owns it
            // (ProceduralSwim::SetProneAngle) because it must be in the same coordinate
            // frame as the arm target directions computed inside Sample. Rotating
            // afterwards left the arms almost motionless - the target direction and the
            // rest direction were nearly parallel, so acos() gave a tiny angle. Only
            // the blend weight scaling still belongs here, and that is now expressed by
            // temporarily overriding the source's prone angle during the transition.
            (void)cur;
            (void)nxt;
        }

            // Real seconds since the previous frame, clamped: the first frame after the
            // model loads blocks for seconds, and integrating a spring over that whole
            // interval would fling every chain.
            character.secondaryDt = std::clamp(static_cast<float>(info.dt), 0.0f, 0.05f);

            // ---- secondary motion: tail / skirt / hair / ears ----
            //
            // Runs BETWEEN the pose evaluation and skinning, and it is the only stage
            // that carries state across frames. Ordering is forced: the solver reads
            // each chain's ROOT world transform, so world matrices must exist first,
            // and it writes LOCAL matrices, so skinning must come after.
            //
            // Assets without these bones leave every chain empty and this loop body
            // never runs - that is what "optional" means here.
            {
                std::vector<glm::mat4> smWorld;
                model.skeleton().ComputeWorld(character.pose, model.Mat4(), smWorld);
                for (int ci = 0; ci < gldx::RigChainKindCount; ++ci) {
                    const auto kind = static_cast<gldx::RigChainKind>(ci);
                    if (!character.rig.hasChain(kind)) continue;
                    if (std::getenv("GLDX_NO_SECONDARY")) continue;   // A/B switch
                    const gldx::ChainTuning& tk = character.chainTune[ci];
                    character.chainSolvers[ci].Solve(character.rig.chains[ci], character.pose,
                                                     smWorld, model.restWorld(), tk,
                                                     character.secondaryDt);
                }
            }

            model.skeleton().ComputeSkinning(character.pose, model.Mat4(), character.palette);




            // Ground contact, solved forward from the pose instead of authored by
            // hand: find the lowest of the four sole points *after* posing, then
            // slide the whole character down/up by that amount. The pelvis bob
            // then falls out of the gait (two dips per cycle, deepest at double
            // support) and it cannot drift out of sync with the legs - which is
            // exactly the failure mode of the first version, where the legs
            // scissored and the feet stayed flat on the floor.
            //
            // The solve is NOT the whole vertical story: a jump asks for its own arc
            // on top of it (`VerticalShiftFor`, which owns the grounded/airborne
            // split). Gating the solve off for the flight and stopping there - which
            // is what this demo used to do - leaves the character hanging at standing
            // height with its legs tucked, because nothing supplies the rise.
            const gldx::GroundContact::Solution solved =
                character.ground.Solve(character.palette);
            const float solvedLift = solved.valid() ? solved.lift : 0.0f;
            // `clipSeconds`, NOT `animSeconds`. The arc functions take a CLIP time; the
            // absolute clock is only equal to it when the action started at t = 0. With
            // `animSeconds` here, pressing 6 three seconds into a session asked the jump
            // arc "how high are you at t = 3 s of a 1.03 s clip", got 0 (clamped past the
            // end), and the character stayed glued to the floor - the whole reason the
            // live jump never left the ground while `--on trace` (which starts at t = 0)
            // showed the arc rising correctly.
            const float shift = VerticalShiftFor(character, view.requestAction, clipSeconds,
                                                 solvedLift);
            view.groundLift = shift;
            view.airRise = (view.requestAction == Action::Jump && character.jumpSource)
                               ? character.jumpSource->AirborneRiseAt(clipSeconds)
                               : 0.0f;
            if (solved.valid() && view.walk) {
                gldx::GroundContact::ApplyLift(character.palette, shift);
            }
            // Where the lowest sole ended up after the shift: the number that says
            // "planted" (0) or "in the air" (>0). Printed in millimetres because
            // "0.000 m" in a panel is indistinguishable from "not measured".
            view.soleHeight = solved.valid() ? (-solved.lift + shift) : 0.0f;
        }

        const float aspect = info.fbHeight > 0
                                 ? static_cast<float>(info.fbWidth) / static_cast<float>(info.fbHeight)
                                 : 1.0f;
        view.orbit.Apply(view.fov, aspect, 0.05f, 200.0f);
        camera = view.orbit.camera();

        gldx::RenderFrame frame;
        frame.fbWidth     = info.fbWidth;
        frame.fbHeight    = info.fbHeight;
        frame.smoothedFps = info.smoothedFps;
        frame.camera      = &camera;
        frame.viewProj    = camera.ViewProjection();
        frame.overlay.sprite = &sprite;
        frame.overlay.font   = &font;
        frame.overlay.white  = &white;

        renderer.Render(frame);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
