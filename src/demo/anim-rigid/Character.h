// The demo's character: the rig plus everything derived from it.
//
// Split out of main.cpp because it is the data model the passes, the probes and the
// frame loop all name. NOTE: names gldx types, so include AFTER `import gldx;`.
#pragma once

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Tunings.h"
#include "Scene.h"
#include "View.h"

namespace anim {

enum class NamingChannel { Auto, Mmd };

// The gait numbers the curves below are written against. An alias rather than a second
// type so the engine's tuning and the demo's cannot drift apart - if someone adds a
// field to `WalkTuning`, it lands here too.
using GaitTuning = gldx::WalkTuning;

// A clamp, an ease and a segment ramp - the three primitives the curves below are
// built from. Naming them keeps the gait readable as a gait instead of as arithmetic.
inline float Clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
inline float Ease01(float x) {
    x = Clamp01(x);
    return x * x * (3.0f - 2.0f * x);
}
inline float Segment01(float x, float a, float b) {
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

inline LegAngles LegAt(const GaitTuning& tune, float p) {
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
struct Character {
    gldx::RiggedModel model;
    gldx::RigProfile  rig;
    gldx::GroundContact ground;

    // Constant vertical offset for the run, in metres, applied instead of the per-frame
    // ground solve.
    //
    // A run has a FLIGHT PHASE, so grounding it every frame (lowest vertex -> 0) would
    // flatten the very thing that makes it read as a run. Leaving it out entirely - the
    // previous behaviour - was worse: the character hovered a constant 0.135 m up, all
    // cycle, with a shadow far below. The right lever is a constant: measure the lowest
    // vertex over one whole cycle and shift the character by -that, so the lowest instant
    // of the cycle plants on the floor and the flight phase keeps its own arc on top.
    float runGroundOffset = 0.0f;
    bool  runGroundReady  = false;

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
    static constexpr float kSeatedHipRatio = 0.338f;
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
    // One-shot settle budget for `--freeze-at` (see the frame loop): the cloth is run
    // to steady state exactly once, so the frozen picture depends on the instant the
    // clock stopped rather than on the frame rate.
    bool  secondarySettled = false;
    // Decimated copy of the mesh, kept for measurements that need real vertices (the
    // contact-point solve has been wrong twice; the mesh does not lie).
    std::vector<gldx::SkinnedVertex> probeVerts;
    int   secondarySettleSteps = 0;

    // Per-kind chain parameters. Values are asset properties (how many panels, how
    // soft), so they live here rather than in the engine - same rule as WalkTuning.
    gldx::ChainTuning chainTune[gldx::RigChainKindCount];
    float gaitSwingL = 0.0f;   // 左膝相对左髋的前向位移（米）
    float gaitSwingR = 0.0f;
    float sitRise    = 0.0f;
    // MMD `捩`（twist）bones that sit directly under the UPPER arm instead of the
    // forearm. Gloves and cuffs are weighted to them, so unless they are rotated with
    // the elbow those pieces stay on the upper arm's line while the forearm swings away
    // - visually "the cuff hangs at the waist and the arm passes through it".
    //
    // Found by name at load (this is demo-side, MMD-specific knowledge).
    std::vector<int> twistBones;
    // For each twist bone: where it sits in the FOREARM's local frame, measured in bind
    // pose. Applying that offset through the posed forearm puts the bone exactly where it
    // would be if the exporter had parented it to the forearm - which is what the cuff
    // geometry assumes. Handles both variants identically (`左腕捩` under the upper arm,
    // `左腕捩` deeper down, etc.) without per-bone special cases.
    std::vector<glm::vec3> twistForearmOffset;   // parallel to twistBones
    // The bone that carries BOTH the legs and the torso: the lowest common ancestor of
    // `hips` and `spine`.
    //
    // On MMD rigs those two are SIBLINGS (`下半身` and `上半身`, both under `腰`), so
    // rotating `hips` moves only the legs and the pelvis never comes down - which is why
    // crouch/sit looked like "standing with bent knees". Translating the LCA moves the
    // whole body, and it is derived from the tree rather than hard-coded, so a rig where
    // hips really is the spine's parent still works (the LCA is then hips itself).
    int bodyRoot = -1;
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

// Metres to add to every palette translation. `solvedLift` is `ground.Solve()`'s
// answer for the pose the caller just built.

inline float GroundWeightFor(const Character& c, Action a, float t) {
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
inline float VerticalShiftFor(const Character& c, Action a, float t, float solvedLift) {
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
    // `restLift` is the lift that puts the STANDING soles on the floor. It is not part
    // of a seated pose's height at all: the seat is what touches, and `sitRise` already
    // places it. Adding the two launched the character 0.87 m into the air - measured
    // `sit <- FLOATS` with `lowestY = 0.871`, i.e. the standing lift was applied on top
    // of a pose that was already on the floor.
    const float flight = (a == Action::Sit) ? rise : (c.restLift + rise);
    // Written as a weighted sum so the walk path stays bit-identical: weight is
    // exactly 1 there, and 1*x + 0*y is x in IEEE arithmetic.
    return weight * solvedLift + (1.0f - weight) * flight;
}

}  // namespace anim
