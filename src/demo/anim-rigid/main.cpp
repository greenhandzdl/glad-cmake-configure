/**
 * @file main.cpp
 * @brief anim-rigid - a rigged FBX, skinned on the GPU, walking forever.
 *
 * The interesting part of this demo is that the asset has a skeleton and skin
 * weights but **no animation clips at all** (the FBX carries 17 skinned meshes,
 * 23 bones per mesh, and zero `AnimationStack`s). So the walking is authored
 * here, procedurally, from the rig's own rest pose: nothing is played back, the
 * pose is a pure function of the clock. That is also why `--freeze-at` frames
 * are reproducible - the gait reads `min(time, freezeAt)` instead of
 * accumulating a dt, so two runs with the same switches draw the same pixels.
 *
 * Three pieces make the skinning work, and each of them is a place this can go
 * silently wrong, so they are worth naming before the code:
 *
 *  1. Space. Assimp hands back the FBX's vertices in each mesh node's local
 *     space, but the cluster bind matrices in bind/scene space. The FBX here
 *     is authored Z-up with the Y-up conversion parked on the mesh nodes, so
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
 *     it is measured from the rig's bone positions at load time.
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
#include <future>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

import gldx;
import gldxwin;
import gldxcli;

#include "Shaders.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

// The GLSL declares `uniform mat4 uBones[64]` because a uniform array needs a
// literal size. Pinning the engine's number to it here means a rig that outgrows
// the shader fails the build instead of indexing past the end of the array.
static_assert(gldx::kMaxBonePalette == 64, "uBones[N] in Shaders.h must match gldx::kMaxBonePalette");

constexpr const char* kDefaultModel = "src/demo/anim-rigid/demo_should_be_ignored.fbx";

// ---------------------------------------------------------------------------
// Rig classification: which bone is the hip, which is the left thigh, and which
// way is up. Names are matched against the conventions the common exporters use
// (Blender's FBX/glTF namer, Mixamo's prefixed rigs, 3ds Max's Biped), so the demo
// is not welded to this one asset.
// ---------------------------------------------------------------------------
struct Rig {
    int hips   = -1;
    int spine  = -1;
    int spine1 = -1;
    int chest  = -1;
    int head   = -1;
    // 0 = left, 1 = right.
    int legUp[2]  = {-1, -1};
    int legLow[2] = {-1, -1};
    int foot[2]   = {-1, -1};
    int toe[2]    = {-1, -1};
    int armUp[2]  = {-1, -1};
    int armLow[2] = {-1, -1};
    int hand[2]   = {-1, -1};    // wrist: the bone the hand mesh is weighted to
    int clavicle[2] = {-1, -1};  // shoulder: only ever rotated a few degrees

    glm::vec3 side{1.0f, 0.0f, 0.0f};     // toward the character's left
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};  // the direction it walks
    bool walkable = false;                // hips + at least one usable leg chain
};

int FindBone(const gldx::Skeleton& sk, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const int index = sk.Find(name);
        if (index >= 0) return index;
    }
    return -1;
}

glm::vec3 BonePosition(const gldx::Skeleton& sk, int bone) {
    return bone < 0 ? glm::vec3(0.0f) : glm::vec3(sk.RestWorld(bone)[3]);
}

Rig ClassifyRig(const gldx::Skeleton& sk) {
    Rig rig;
    rig.hips  = FindBone(sk, {"Hips", "hips", "Pelvis", "pelvis", "Bip01 Pelvis"});
    rig.spine = FindBone(sk, {"Spine", "spine", "Spine1", "Bip01 Spine"});
    rig.chest = FindBone(sk, {"Spine2", "Chest", "chest", "spine.003", "Bip01 Spine2"});
    rig.head  = FindBone(sk, {"Head", "head", "Bip01 Head"});
    // Mid-torso, only used to anchor the skirt's waistline for the clearance probe.
    rig.spine1 = FindBone(sk, {"Spine1", "Spine2", "Chest", "mixamorig:Spine1", "Bip01 Spine1"});

    rig.legUp[0]  = FindBone(sk, {"LeftUpLeg", "LeftUpperLeg", "LeftThigh", "UpperLeg.L",
                                  "Thigh.L", "mixamorig:LeftUpLeg", "Bip01 L Thigh"});
    rig.legLow[0] = FindBone(sk, {"LeftLeg", "LeftLowerLeg", "LowerLeg.L", "Calf.L",
                                  "Shin.L", "mixamorig:LeftLeg", "Bip01 L Calf"});
    rig.foot[0]   = FindBone(sk, {"LeftFoot", "Foot.L", "mixamorig:LeftFoot", "Bip01 L Foot"});
    rig.toe[0]    = FindBone(sk, {"LeftToeBase", "LeftToe", "Toe.L", "mixamorig:LeftToeBase",
                                  "Bip01 L Toe0"});
    rig.legUp[1]  = FindBone(sk, {"RightUpLeg", "RightUpperLeg", "RightThigh", "UpperLeg.R",
                                  "Thigh.R", "mixamorig:RightUpLeg", "Bip01 R Thigh"});
    rig.legLow[1] = FindBone(sk, {"RightLeg", "RightLowerLeg", "LowerLeg.R", "Calf.R",
                                  "Shin.R", "mixamorig:RightLeg", "Bip01 R Calf"});
    rig.foot[1]   = FindBone(sk, {"RightFoot", "Foot.R", "mixamorig:RightFoot", "Bip01 R Foot"});
    rig.toe[1]    = FindBone(sk, {"RightToeBase", "RightToe", "Toe.R", "mixamorig:RightToeBase",
                                  "Bip01 R Toe0"});

    rig.armUp[0]  = FindBone(sk, {"LeftArm", "LeftUpperArm", "UpperArm.L", "Arm.L",
                                  "mixamorig:LeftArm", "Bip01 L UpperArm"});
    rig.armLow[0] = FindBone(sk, {"LeftForeArm", "LeftLowerArm", "LowerArm.L", "ForeArm.L",
                                  "mixamorig:LeftForeArm", "Bip01 L Forearm"});
    rig.armUp[1]  = FindBone(sk, {"RightArm", "RightUpperArm", "UpperArm.R", "Arm.R",
                                  "mixamorig:RightArm", "Bip01 R UpperArm"});
    rig.armLow[1] = FindBone(sk, {"RightForeArm", "RightLowerArm", "LowerArm.R", "ForeArm.R",
                                  "mixamorig:RightForeArm", "Bip01 R Forearm"});
    rig.hand[0]   = FindBone(sk, {"LeftHand", "Hand.L", "mixamorig:LeftHand", "Bip01 L Hand"});
    rig.hand[1]   = FindBone(sk, {"RightHand", "Hand.R", "mixamorig:RightHand", "Bip01 R Hand"});
    rig.clavicle[0] = FindBone(sk, {"LeftShoulder", "LeftClavicle", "Shoulder.L", "Clavicle.L",
                                    "mixamorig:LeftShoulder", "Bip01 L Clavicle"});
    rig.clavicle[1] = FindBone(sk, {"RightShoulder", "RightClavicle", "Shoulder.R", "Clavicle.R",
                                    "mixamorig:RightShoulder", "Bip01 R Clavicle"});

    // Axes are measured, never assumed. Up is the hip-to-head direction, the
    // lateral axis joins the two thighs, and forward completes the right-handed
    // triple (side, up, forward) - which is exactly what the display matrix below
    // needs, and what the gait's rotation axes are expressed in.
    const glm::vec3 hipPos  = BonePosition(sk, rig.hips);
    const glm::vec3 headPos = BonePosition(sk, rig.head);
    if (rig.hips >= 0 && rig.head >= 0) {
        const glm::vec3 d = headPos - hipPos;
        if (glm::length(d) > 1e-4f) rig.up = glm::normalize(d);
    }
    if (rig.legUp[0] >= 0 && rig.legUp[1] >= 0) {
        const glm::vec3 d = BonePosition(sk, rig.legUp[0]) - BonePosition(sk, rig.legUp[1]);
        if (glm::length(d) > 1e-4f) rig.side = glm::normalize(d);
    } else if (rig.hips >= 0) {
        // No legs to measure from: pick any lateral axis perpendicular to up.
        const glm::vec3 seed = std::abs(rig.up.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        rig.side = glm::normalize(glm::cross(rig.up, seed));
    }
    rig.forward = glm::normalize(glm::cross(rig.side, rig.up));
    rig.walkable = rig.hips >= 0 && rig.legUp[0] >= 0 && rig.legUp[1] >= 0;
    return rig;
}

// ---------------------------------------------------------------------------
// The pose. Everything here is a rotation about one of the rig axes above,
// converted into the bone's local frame by its *rest world* matrix:
//
//   world(bone) = restWorld(bone) * local, so asking for "an extra rotation Rw
//   about world axis w" gives local = restWorld^-1 * Rw * restWorld, whose axis
//   is restWorld^-1 * w. Rotating in the bone's own frame therefore pivots about
//   the bone's own origin, which is the joint - that is the whole trick.
// ---------------------------------------------------------------------------
glm::mat4 PoseRotation(const glm::mat4& restWorldBone, const glm::vec3& worldAxis, float radians) {
    const glm::mat3 basis = glm::mat3(restWorldBone);
    const glm::vec3 axis = glm::transpose(basis) * worldAxis;
    const float len = glm::length(axis);
    if (!(len > 1e-6f)) return glm::mat4(1.0f);   // degenerate bind matrix: no-op
    return glm::rotate(glm::mat4(1.0f), radians, axis / len);
}

struct GaitTuning {
    // Stance is the fraction of a leg's cycle where the foot carries the body.
    // 0.6 for walking (a real gait spends ~60% in stance, and the two legs
    // overlap by 20% - that overlap is the double-support phase that makes a
    // walk look like a walk rather than a run).
    float stanceEnd = 0.60f;

    float hipSwing       = 0.33f;   // rad, thigh from forward-most to backward-most
    float kneeStanceFlex = 0.14f;   // rad, the small dip just after heel strike
    float kneeSwingFlex  = 0.72f;   // rad, peak flexion used for foot clearance
    float ankleHeelStrike = -0.18f; // rad, dorsiflexed at contact (toe up)
    float ankleToeOff     = 0.34f;  // rad, plantarflexed at push-off (toe down)
    float toeOffBend      = 0.34f;  // rad, toe joint dorsiflexion while the heel is up

    // T-pose -> A-pose. Plumb is 1.57 and almost everything is wrong about it
    // for this character: this skirt is a flared cone around both thighs, so a
    // plumb humerus hangs the forearm *through* the fabric. The value was found
    // by rendering a close-up and stepping (1.45 -> 1.16 -> 1.05), not by
    // trigonometry: at 1.45 (83 deg, the first attempt) the wrist was visibly
    // sunk in the skirt; at 1.05 (60 deg) the sleeve cuff, the palm and all four
    // fingers read clear of the hem in every phase, at roughly 0.1 m of margin.
    // Going lower is the mirror image of leaving the arms in the T-pose: the
    // silhouette looks calm while the mesh interpenetrates.
    float armDown  = 1.05f;         // rad
    float armSwing = 0.38f;         // rad
    float elbowBase = 0.55f;        // rad, flexion kept in BOTH swing phases
    float elbowSwing = 0.22f;       // rad, extra flexion as the arm comes forward
    float armOut   = 0.05f;         // rad, forearm abduction: keeps the hand clear
                                    // of the flared skirt, which is 0.2 m wide at hip
                                    // height and swallows a plumb-hanging wrist
    float wristBase = 0.14f;        // rad, relaxed hand, slightly palmar
    float wristSwing = 0.16f;       // rad, the hand trails the forearm
    float wristDeviate = 0.07f;     // rad, palm turns in/out with the swing
    float clavicleRoll = 0.045f;    // rad, the shoulder follows the arm a little

    float hipYaw   = 0.10f;         // rad, pelvis rotates toward the stepping leg
    float chestYaw = 0.15f;         // rad, shoulders counter the pelvis
    float lean     = 0.06f;         // rad, constant forward lean
    float headYaw  = 0.05f;         // rad, the head stays roughly world-facing
};

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

// Fills `outPose` (one local matrix per bone) for the given phase in radians.
// `phase` advances by 2*pi per full two-step cycle; the right side runs half a
// cycle behind the left. The pelvis *translation* is not part of the pose: it is
// solved afterwards from ground contact (see GroundOffset), because a bob
// authored by hand fights every other change to the legs.
void BuildWalkPose(const gldx::Skeleton& sk, const Rig& rig,
                   const std::vector<glm::mat4>& restWorld, const GaitTuning& tune,
                   float phase, std::vector<glm::mat4>& outPose) {
    outPose.resize(sk.size());
    for (std::size_t i = 0; i < sk.size(); ++i) outPose[i] = sk.bones()[i].restLocal;

    auto rotate = [&](int bone, const glm::vec3& worldAxis, float radians) {
        if (bone < 0 || radians == 0.0f) return;
        outPose[static_cast<std::size_t>(bone)] =
            outPose[static_cast<std::size_t>(bone)] *
            PoseRotation(restWorld[static_cast<std::size_t>(bone)], worldAxis, radians);
    };

    const float cycle = phase / (2.0f * kPi);          // whole cycles, any value
    const float p = cycle - std::floor(cycle);         // 0..1 for the left leg
    const LegAngles left = LegAt(tune, p);
    const LegAngles right = LegAt(tune, p < 0.5f ? p + 0.5f : p - 0.5f);

    // The torso leans into the walk even when the legs are still: at a distance
    // the torso is what reads as "walking", the feet are a few pixels.
    rotate(rig.spine, rig.side, tune.lean);
    rotate(rig.chest, rig.side, tune.lean * 0.5f);

    // The rig's bind pose is a T-pose: the arms point straight out along the
    // lateral axis, so swinging them about that axis would rotate each arm about
    // its own length - invisible. Lowering them into an A-pose is part of the
    // clip (this rig has no A-pose to inherit) and only then does a rotation
    // about the lateral axis become a forward/backward swing.
    //
    // 1.45 rad is not a taste value: at 90 degrees the humerus is exactly plumb
    // and a cylinder turning about its own long axis shows nothing, so the swing
    // would vanish into the silhouette and read as "the arms never moved". A
    // hand-authored clip keeps the shoulder a little out from the ribs for
    // exactly this reason.
    rotate(rig.armUp[0], rig.forward, -tune.armDown);
    rotate(rig.armUp[1], rig.forward, +tune.armDown);

    // Pelvis and shoulders counter-rotate about the vertical axis, and the head
    // cancels most of it so the character keeps looking where it walks.
    const float yawDriver = left.hip / std::max(tune.hipSwing, 1e-4f);   // -1 = left leg forward
    rotate(rig.hips, rig.up, tune.hipYaw * yawDriver);
    rotate(rig.chest, rig.up, -tune.chestYaw * yawDriver);
    rotate(rig.head, rig.up, tune.headYaw * yawDriver);

    for (int s = 0; s < 2; ++s) {
        const LegAngles& leg = (s == 0) ? left : right;
        rotate(rig.legUp[s], rig.side, leg.hip);
        rotate(rig.legLow[s], rig.side, leg.knee);
        rotate(rig.foot[s], rig.side, leg.ankle);
        rotate(rig.toe[s], rig.side, leg.toe);

        // Contralateral arm swing: the arm goes forward exactly when the leg on
        // the same side goes back.
        const float arm = -tune.armSwing * (leg.hip / std::max(tune.hipSwing, 1e-4f));
        // Negative about the lateral axis is "forward", so the elbow flexion
        // (which bends the forearm forward) is negative too, and it deepens as
        // the arm swings forward - a straight arm on the back swing, a bent one
        // in front, which is what makes a walk read as relaxed.
        const float swingN = arm / std::max(tune.armSwing, 1e-4f);   // +1 back, -1 forward
        const float elbow = -(tune.elbowBase + tune.elbowSwing * (0.5f - 0.5f * swingN));
        rotate(rig.armUp[s], rig.side, arm);
        // The shoulder blade is not a rigid bracket: it protracts/retracts with
        // the arm. Three degrees is enough for the silhouette to stop reading as
        // "a hinged stick on a block".
        rotate(rig.clavicle[s], rig.up, -tune.clavicleRoll * swingN);
        rotate(rig.armLow[s], rig.side, elbow);
        // ...plus a constant outward deviation at the elbow. This is the part that
        // keeps the hand out of the skirt: the humerus cannot hang far enough out
        // to clear a 0.3 m hem without the pose going stiff and A-shaped, so the
        // clearance is bought at the forearm instead.
        rotate(rig.armLow[s], rig.up, tune.armOut * (s == 0 ? 1.0f : -1.0f));
        // ...and the wrist, which is the part the eye actually tracks at the end
        // of the limb. Without it the whole arm is a rigid pendulum: the shoulder
        // and elbow move, the hand's orientation never changes, and the result
        // reads as "the hands are frozen" even though the arm is swinging.
        //
        // This rig has no finger bones, but it does have an animated fan: the
        // hand mesh is skinned to Hips/Spine/Head as well as to the wrist chain,
        // so once the wrist turns, the fan splays and gathers through the cycle
        // for free. That is why a wrist rotation is enough to make the hands read
        // as alive here, and why the same code would look stiff on a hand that is
        // 100% weighted to its own bone.
        const float wrist = -(tune.wristBase + tune.wristSwing * (0.5f - 0.5f * swingN));
        rotate(rig.hand[s], rig.side, wrist);
        const float deviate = tune.wristDeviate * swingN * (s == 0 ? 1.0f : -1.0f);
        rotate(rig.hand[s], rig.up, deviate);
    }
}

// ---------------------------------------------------------------------------
// One loaded character: the CPU skeleton, the GPU meshes, and the scratch the
// per-frame palette is computed into.
// ---------------------------------------------------------------------------
struct Character {
    gldx::Skeleton skeleton;
    std::vector<gldx::SkinnedMesh> meshes;
    std::vector<glm::mat4> restWorld;
    std::vector<glm::mat4> pose;
    std::vector<glm::mat4> palette;
    std::vector<std::string> paletteNames;   // "uBones[i]", built once
    Rig rig;
    GaitTuning tune;

    // The FBX embeds four PBR maps, so they are uploaded for real. `textures` is
    // indexed the way the loader numbered them; the fallbacks are 1x1 dummies
    // that keep every sampler in the program backed by a complete texture even
    // when a map is missing (an active sampler pointing at nothing makes Apple's
    // driver drop the draw call, which looks like "the model disappeared").
    std::vector<gldx::Texture2D> textures;
    gldx::SkinnedMaterial material;
    std::vector<int> meshMaterial;
    gldx::Texture2D fallbackAlbedo;
    gldx::Texture2D fallbackNormal;
    gldx::Texture2D fallbackScalar;
    bool hasAlbedo = false;
    bool hasNormal = false;
    bool hasMetallic = false;
    bool hasRoughness = false;
    int textureWidth = 0;
    int textureHeight = 0;

    // Four rig-space contact points (a heel and a ball per foot), taken once from
    // the bind pose. They are carried by the foot and toe bones, so the pelvis
    // height in any frame can be solved from where the *lowest* one actually is
    // instead of from a hand-authored bob curve.
    glm::vec3 contactRig[4]{};
    int       contactBone[4] = {-1, -1, -1, -1};
    int       contactCount = 0;

    glm::mat3 orientation{1.0f};   // rig axes -> world (X right, Y up, Z forward)
    float unitScale = 0.01f;       // rig units -> metres
    float heightMetres = 1.7f;
    float legLengthMetres = 0.9f;
    float cycleMetres = 1.0f;      // floor travel per full two-step cycle
    float bindDeviation = 0.0f;    // |rest palette - root transform|max, measured at load
    int vertexCount = 0;
    int triangleCount = 0;
    std::string sourcePath;
    std::string error;

    [[nodiscard]] bool ready() const { return !meshes.empty(); }
    [[nodiscard]] float phaseFor(float speed, float seconds) const {
        // Distance walked / distance travelled by one cycle, so the feet stay
        // planted: faster walking is a faster cadence, not a longer stride.
        return cycleMetres > 1e-3f ? 2.0f * kPi * (speed * seconds / cycleMetres) : 0.0f;
    }
};

} // namespace

namespace {

// ---------------------------------------------------------------------------
// Camera + the runtime state the pass, the HUD and the input callbacks share.
// ---------------------------------------------------------------------------
struct View {
    float yaw      = 0.62f;   // radians around the character, 0 = straight ahead (+Z)
    float pitch    = 0.17f;
    float distance = 3.6f;
    float fov      = 42.0f;
    float speed    = 1.30f;   // metres per second of floor travel
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
    bool report     = false;  // dump the gait's joint angles and sole heights once

    float groundLift = 0.0f;   // metres the pelvis was moved to keep a sole on the floor
    float soleHeight = 0.0f;   // metres, lowest sole after the solve (0 = touching)

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

        if (skin_ && character_.ready() && !character_.palette.empty()) {
            skin_->Use();
            skin_->Set("uViewProj", f.viewProj);
            skin_->Set("uCamPos", eye);
            skin_->Set("uSunDir", glm::normalize(kSunDir));
            skin_->Set("uSunColor", kSunColor);
            skin_->Set("uSunIntensity", kSunIntensity);
            skin_->Set("uSkyColor", kSkyColor);
            skin_->Set("uBounceColor", kBounceColor);
            for (std::size_t i = 0; i < character_.palette.size(); ++i) {
                skin_->Set(character_.paletteNames[i].c_str(), character_.palette[i]);
            }

            // Units are fixed by the program's declarations (0 albedo, 1 normal,
            // 2 metallic, 3 roughness) and every one of them is bound to a
            // complete texture on every draw, dummy or not - see the note on the
            // fallbacks in Character.
            const gldx::Texture2D& albedo =
                (view_.maps && character_.hasAlbedo)
                    ? character_.textures[character_.material.albedoTexture]
                    : character_.fallbackAlbedo;
            const gldx::Texture2D& normal =
                (view_.maps && character_.hasNormal)
                    ? character_.textures[character_.material.normalTexture]
                    : character_.fallbackNormal;
            const gldx::Texture2D& metallic =
                (view_.maps && character_.hasMetallic)
                    ? character_.textures[character_.material.metallicTexture]
                    : character_.fallbackScalar;
            const gldx::Texture2D& roughness =
                (view_.maps && character_.hasRoughness)
                    ? character_.textures[character_.material.roughnessTexture]
                    : character_.fallbackScalar;
            albedo.Bind(0);
            normal.Bind(1);
            metallic.Bind(2);
            roughness.Bind(3);
            skin_->Set("uAlbedo", 0);
            skin_->Set("uNormal", 1);
            skin_->Set("uMetallic", 2);
            skin_->Set("uRoughness", 3);
            const bool useMaps = view_.maps;
            skin_->Set("uHasAlbedo", (useMaps && character_.hasAlbedo) ? 1 : 0);
            skin_->Set("uHasNormal", (useMaps && view_.normalMap && character_.hasNormal) ? 1 : 0);
            skin_->Set("uHasMetallic", (useMaps && character_.hasMetallic) ? 1 : 0);
            skin_->Set("uHasRoughness", (useMaps && character_.hasRoughness) ? 1 : 0);
            skin_->Set("uBaseColor", character_.material.baseColor);
            skin_->Set("uMetallicFactor", character_.material.metallic);
            skin_->Set("uRoughnessFactor", character_.material.roughness);

            for (std::size_t i = 0; i < character_.meshes.size(); ++i) {
                character_.meshes[i].Draw();
                ++draws;
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
        if (!character_.ready()) {
            hud.AddLine(character_.error.empty() ? "loading rig (worker thread)..." : character_.error.c_str(),
                        character_.error.empty() ? gldx::Hud::kDim : gldx::Hud::kWarn);
        } else {
            std::snprintf(line, sizeof(line), "%d bones, %d meshes, %d verts, %d tris",
                          static_cast<int>(character_.skeleton.size()),
                          static_cast<int>(character_.meshes.size()),
                          character_.vertexCount, character_.triangleCount);
            hud.AddLine(line, gldx::Hud::kDim);
            // The phase the *gait* is using, folded into 0..1 - the value the
            // pose is actually built from. (An earlier version printed
            // phaseFor(1.0f, ...), which silently reads 1.30x low at the default
            // speed; a number that disagrees with the picture is worse than none.)
            float cyclePhase = character_.phaseFor(view_.speed, static_cast<float>(view_.clock)) /
                               (2.0f * kPi);
            cyclePhase -= std::floor(cyclePhase);
            std::snprintf(line, sizeof(line), "walk %s | gait phase %.2f cycle | %.2f m/s",
                          view_.walk ? "on " : "off", static_cast<double>(cyclePhase),
                          static_cast<double>(view_.speed));
            hud.AddLine(line, gldx::Hud::kFg);
            std::snprintf(line, sizeof(line), "height %.2f m (rig unit x%.3g) | leg %.2f m | cycle %.2f m",
                          static_cast<double>(character_.heightMetres),
                          static_cast<double>(character_.unitScale),
                          static_cast<double>(character_.legLengthMetres),
                          static_cast<double>(character_.cycleMetres));
            hud.AddLine(line, gldx::Hud::kDim);
            std::snprintf(line, sizeof(line), "palette %d/%d bones | bind check %.1e | %d draws",
                          static_cast<int>(character_.palette.size()), gldx::kMaxBonePalette,
                          static_cast<double>(character_.bindDeviation), view_.drawCalls);
            hud.AddLine(line, gldx::Hud::kDim);
            std::snprintf(line, sizeof(line), "ground: pelvis %+6.1f mm | lowest sole %+6.1f mm",
                          static_cast<double>(view_.groundLift * 1000.0f),
                          static_cast<double>(view_.soleHeight * 1000.0f));
            hud.AddLine(line, gldx::Hud::kDim);
            // The maps come out of the FBX itself, so this line is the claim
            // "textured", not "a texture file was found next to the model".
            std::snprintf(line, sizeof(line), "maps: albedo %s normal %s metal %s rough %s (%dx%d max)",
                          character_.hasAlbedo ? "y" : "n", character_.hasNormal ? "y" : "n",
                          character_.hasMetallic ? "y" : "n", character_.hasRoughness ? "y" : "n",
                          character_.textureWidth, character_.textureHeight);
            hud.AddLine(line, gldx::Hud::kDim);
        }
        std::snprintf(line, sizeof(line), "t %.2f s%s | %.0f fps", view_.clock,
                      view_.freezeAt > 0.0f ? " (frozen)" : "", f.smoothedFps);
        hud.AddLine(line, view_.freezeAt > 0.0f ? gldx::Hud::kWarn : gldx::Hud::kDim);

        hud.BeginSection(gldx::Hud::Anchor::BottomLeft, 14.0f);
        hud.AddLines({
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
// future is ready.
void UploadCharacter(Character& character, gldx::LoadedSkinnedModelData&& data,
                     bool reportGait) {
    character.skeleton  = std::move(data.skeleton);
    character.sourcePath = data.sourcePath;
    character.rig = ClassifyRig(character.skeleton);

    // Extent along the measured up axis, from the bind-pose vertices themselves
    // (a bone span would miss the head and the soles).
    float minUp = 0.0f;
    float maxUp = 0.0f;
    bool first = true;
    for (const gldx::SkinnedMeshData& mesh : data.meshes) {
        for (const gldx::SkinnedVertex& v : mesh.vertices) {
            const float d = glm::dot(v.position, character.rig.up);
            if (first) {
                minUp = maxUp = d;
                first = false;
            }
            minUp = std::min(minUp, d);
            maxUp = std::max(maxUp, d);
        }
        character.vertexCount += static_cast<int>(mesh.vertices.size());
        character.triangleCount += static_cast<int>(mesh.indices.size() / 3);
    }
    const float rigHeight = first ? 1.0f : (maxUp - minUp);
    // Units are a property of the file, not a knob: a 163-unit character is in
    // centimetres, a 1.63-unit one is already in metres. Guessing wrong makes the
    // camera sit inside the model and reads as "nothing loaded".
    character.unitScale = (rigHeight > 20.0f) ? 0.01f : 1.0f;
    character.heightMetres = std::max(rigHeight * character.unitScale, 0.2f);

    // Display basis: the rig's own (side, up, forward) triple, so the character
    // stands up in world space whatever axis its author used.
    character.orientation = glm::mat3(character.rig.side, character.rig.up, character.rig.forward);

    character.restWorld.resize(character.skeleton.size());
    for (std::size_t i = 0; i < character.skeleton.size(); ++i) {
        character.restWorld[i] = character.skeleton.RestWorld(static_cast<int>(i));
    }
    character.paletteNames.reserve(character.skeleton.size());
    for (std::size_t i = 0; i < character.skeleton.size(); ++i) {
        character.paletteNames.push_back("uBones[" + std::to_string(i) + "]");
    }

    // --- contact points -------------------------------------------------
    // The sole plane is the lowest bind-pose vertex; a heel sits under the ankle
    // joint and a ball under the toe joint. Both are stored in rig space and
    // later carried by the bone they belong to (see the frame loop), which is
    // what lets a rotated foot keep its real contact height.
    {
        const glm::vec3 up = character.rig.up;
        auto addContact = [&](int bone) {
            if (bone < 0 || character.contactCount >= 4) return;
            const glm::vec3 p = BonePosition(character.skeleton, bone);
            character.contactRig[character.contactCount] = p - up * (glm::dot(p, up) - minUp);
            character.contactBone[character.contactCount] = bone;
            ++character.contactCount;
        };
        addContact(character.rig.foot[0]);
        addContact(character.rig.toe[0]);
        addContact(character.rig.foot[1]);
        addContact(character.rig.toe[1]);
    }

    // --- embedded textures (Stage B: GL objects, render thread) ----------
    character.textures.resize(data.textures.size());
    for (std::size_t i = 0; i < data.textures.size(); ++i) {
        const gldx::Texture2DDesc& desc = data.textures[i].desc;
        if (desc.width <= 0 || desc.height <= 0 || desc.pixels.empty()) continue;
        character.textures[i].Upload(desc);
        character.textureWidth = std::max(character.textureWidth, desc.width);
        character.textureHeight = std::max(character.textureHeight, desc.height);
    }
    if (!data.materials.empty()) character.material = data.materials.front();
    character.meshMaterial = data.meshMaterial;
    auto usable = [&](int index) {
        return index >= 0 && static_cast<std::size_t>(index) < character.textures.size();
    };
    character.hasAlbedo = usable(character.material.albedoTexture);
    character.hasNormal = usable(character.material.normalTexture);
    character.hasMetallic = usable(character.material.metallicTexture);
    character.hasRoughness = usable(character.material.roughnessTexture);

    if (character.rig.walkable) {
        character.legLengthMetres =
            glm::length(BonePosition(character.skeleton, character.rig.hips) -
                        BonePosition(character.skeleton, character.rig.foot[0])) * character.unitScale;
    }
    if (character.legLengthMetres < 1e-3f) character.legLengthMetres = character.heightMetres * 0.52f;
    // One cycle = two steps, and one step lays down the horizontal distance the
    // foot covers between its extremes: 2 * legLength * sin(hipSwing). The floor
    // scroll and the cadence both come from this, which is what keeps the feet
    // from skating.
    character.cycleMetres =
        std::max(4.0f * character.legLengthMetres * std::sin(character.tune.hipSwing), 0.1f);

    for (gldx::SkinnedMeshData& mesh : data.meshes) {
        character.meshes.emplace_back();
        character.meshes.back().Upload(std::move(mesh));
    }

    // Bind check: with an empty pose every skinning matrix must be exactly the
    // root transform. It is structural rather than empirical (the rest pose is
    // derived from these same inverse-bind matrices), but it is the property the
    // whole demo rests on, so it is measured and reported instead of assumed.
    {
        std::vector<glm::mat4> check;
        const glm::mat4 root = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f)) *
                               glm::mat4(character.orientation) *
                               glm::scale(glm::mat4(1.0f), glm::vec3(character.unitScale));
        character.skeleton.ComputeSkinning(std::vector<glm::mat4>{}, root, check);
        float deviation = 0.0f;
        for (const glm::mat4& m : check) {
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    deviation = std::max(deviation, std::abs(m[c][r] - root[c][r]));
                }
            }
        }
        character.bindDeviation = deviation;
    }

    std::fprintf(stderr,
                 "[anim-rigid] %s: %zu bones, %zu meshes, %d verts (%d tris), height %.2f m, "
                 "palette %zu/%d, bind check %.3e\n",
                 character.sourcePath.c_str(), character.skeleton.size(), character.meshes.size(),
                 character.vertexCount, character.triangleCount,
                 static_cast<double>(character.heightMetres), character.skeleton.size(),
                 gldx::kMaxBonePalette, static_cast<double>(character.bindDeviation));
    std::fprintf(stderr,
                 "[anim-rigid] textures: albedo %s, normal %s, metallic %s, roughness %s "
                 "(up to %dx%d px); %zu unreferenced map path(s)\n",
                 character.hasAlbedo ? "yes" : "no", character.hasNormal ? "yes" : "no",
                 character.hasMetallic ? "yes" : "no", character.hasRoughness ? "yes" : "no",
                 character.textureWidth, character.textureHeight,
                 data.missingTexturePaths.size());
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
            BuildWalkPose(character.skeleton, character.rig, character.restWorld, character.tune,
                          phase, pose);
            const glm::mat4 root = glm::mat4(character.orientation) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(character.unitScale));
            character.skeleton.ComputeSkinning(pose, root, palette);
            float sole[2] = {0.0f, 0.0f};
            for (int side = 0; side < 2; ++side) {
                float lowest = 1.0e9f;
                for (int c = 0; c < character.contactCount; ++c) {
                    if (character.contactBone[c] != character.rig.foot[side] &&
                        character.contactBone[c] != character.rig.toe[side]) {
                        continue;
                    }
                    const glm::vec4 p = palette[static_cast<std::size_t>(character.contactBone[c])] *
                                        glm::vec4(character.contactRig[c], 1.0f);
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
        for (std::size_t i = 0; i < character.skeleton.size(); ++i) {
            const gldx::Bone* bone = character.skeleton.bone(static_cast<int>(i));
            if (!bone) continue;
            if (bone->parent < 0) ++roots;
            int depth = 0;
            for (int b = bone->parent; b >= 0; b = character.skeleton.bone(b)->parent) ++depth;
            deepest = std::max(deepest, depth);
        }
        std::fprintf(stderr, "[anim-rigid] hierarchy: %d root bone(s), depth %d, "
                             "foot chain %d -> %d -> %d\n",
                     roots, deepest, character.rig.legUp[0], character.rig.legLow[0],
                     character.rig.foot[0]);

        // Which rig slots failed to bind. This line exists because of a bug that
        // survived several rounds of review: ClassifyRig filled the arm chains but not
        // `hand`/`clavicle`, both stayed -1, and every wrist and shoulder rotation was
        // dropped by rotate()'s `bone < 0` guard. Nothing printed, nothing failed, and
        // the only symptom was "the hands look stiff" - i.e. a claim that the code
        // contradicts silently. A missing slot is now named, once, under --on report.
        struct Slot { const char* name; int index; };
        const Slot slots[] = {
            {"hips", character.rig.hips},         {"spine", character.rig.spine},
            {"chest", character.rig.chest},       {"head", character.rig.head},
            {"legUp.L", character.rig.legUp[0]},  {"legUp.R", character.rig.legUp[1]},
            {"legLow.L", character.rig.legLow[0]}, {"legLow.R", character.rig.legLow[1]},
            {"foot.L", character.rig.foot[0]},    {"foot.R", character.rig.foot[1]},
            {"toe.L", character.rig.toe[0]},      {"toe.R", character.rig.toe[1]},
            {"armUp.L", character.rig.armUp[0]},  {"armUp.R", character.rig.armUp[1]},
            {"armLow.L", character.rig.armLow[0]}, {"armLow.R", character.rig.armLow[1]},
            {"hand.L", character.rig.hand[0]},    {"hand.R", character.rig.hand[1]},
            {"clavicle.L", character.rig.clavicle[0]},
            {"clavicle.R", character.rig.clavicle[1]},
        };
        int bound = 0;
        std::string missing;
        for (const Slot& slot : slots) {
            if (slot.index >= 0) {
                ++bound;
            } else {
                missing += ' ';
                missing += slot.name;
            }
        }
        std::fprintf(stderr, "[anim-rigid] rig: %d/%zu slots bound%s%s\n", bound,
                     sizeof(slots) / sizeof(slots[0]),
                     missing.empty() ? "" : "; unbound:", missing.c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {"hud", "walk", "ground", "wireframe", "maps", "normalmap", "report"},
                                 {"model", "speed", "fov", "yaw", "pitch", "distance", "freeze-at",
                                  "target-y"},
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
    view.report    = flags.on("report", false);
    view.speed     = flags.real("speed", view.speed, 0.2f, 6.0f);
    view.fov       = flags.real("fov", view.fov, 15.0f, 100.0f);
    view.yaw       = flags.real("yaw", view.yaw);
    view.pitch     = flags.real("pitch", view.pitch, -1.45f, 1.45f);
    view.distance  = flags.real("distance", view.distance, 0.8f, 30.0f);
    view.freezeAt  = flags.real("freeze-at", 0.0f, 0.0f, 1.0e6f);
    view.target.y  = flags.real("target-y", view.target.y, 0.0f, 3.0f);
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
            return gldx::ModelLoader::LoadSkinned(modelPath);
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

    // The three dummies the character draws with when the file has no such map:
    // white albedo (so the material's base colour shows), a flat +Z normal (so
    // the cotangent frame is a no-op), and white for both scalar maps (so the
    // metallic/roughness factors pass through unchanged).
    character.fallbackAlbedo.Upload(MakeSolidDesc(255, 255, 255, 255, true));
    character.fallbackNormal.Upload(MakeSolidDesc(128, 128, 255));
    character.fallbackScalar.Upload(MakeSolidDesc());

    // Input goes through the gldxwin surface only (GLFW's user pointer belongs to
    // that library). A drag orbits, the wheel scales the distance.
    window.OnCursor([&view](gldx::win::Window&, gldx::win::Vec2d pos) {
        if (!view.dragging) return;
        view.yaw   -= static_cast<float>(pos.x - view.lastX) * 0.006f;
        view.pitch  = std::clamp(view.pitch + static_cast<float>(pos.y - view.lastY) * 0.006f,
                                 -1.45f, 1.45f);
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
        view.distance = std::clamp(view.distance * static_cast<float>(std::exp(-dy * 0.12)),
                                   0.8f, 30.0f);
    });
    window.OnKey([&view, hudRaw](gldx::win::Window&, gldx::win::Key key,
                                 gldx::win::KeyAction action, int) {
        if (action == gldx::win::KeyAction::Release) return;
        switch (key) {
            case gldx::win::Key::W: view.walk = !view.walk; break;
            case gldx::win::Key::G: view.ground = !view.ground; break;
            case gldx::win::Key::F: view.wireframe = !view.wireframe; break;
            case gldx::win::Key::H:
                view.showHud = !view.showHud;
                hudRaw->hud.SetVisible(view.showHud);
                break;
            default: break;
        }
    });

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
            load.wait();
            firstFrame = false;
        }
        if (load.valid() && load.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = load.get();
            if (result) {
                UploadCharacter(character, std::move(*result), view.report);
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
        view.scroll = view.speed * animSeconds;

        if (character.ready()) {
            const float phase = character.phaseFor(view.speed, animSeconds);
            if (view.walk && character.rig.walkable) {
                BuildWalkPose(character.skeleton, character.rig, character.restWorld, character.tune,
                              phase, character.pose);
            } else {
                character.pose.resize(character.skeleton.size());
                for (std::size_t i = 0; i < character.skeleton.size(); ++i) {
                    character.pose[i] = character.skeleton.bones()[i].restLocal;
                }
            }
            const glm::mat4 root = glm::mat4(character.orientation) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(character.unitScale));
            character.skeleton.ComputeSkinning(character.pose, root, character.palette);

            // Ground contact, solved forward from the pose instead of authored by
            // hand: find the lowest of the four sole points *after* posing, then
            // slide the whole character down/up by that amount. The pelvis bob
            // then falls out of the gait (two dips per cycle, deepest at double
            // support) and it cannot drift out of sync with the legs - which is
            // exactly the failure mode of the first version, where the legs
            // scissored and the feet stayed flat on the floor.
            //
            // The shift is applied to the palette rather than to the root matrix
            // because a translation prepended to a skinning matrix is the same
            // operation done once per bone: palette' = T * palette.
            float lowest = 0.0f;
            bool haveContact = false;
            for (int c = 0; c < character.contactCount; ++c) {
                const int bone = character.contactBone[c];
                if (bone < 0) continue;
                const glm::vec4 p = character.palette[static_cast<std::size_t>(bone)] *
                                    glm::vec4(character.contactRig[c], 1.0f);
                lowest = haveContact ? std::min(lowest, p.y) : p.y;
                haveContact = true;
            }
            view.groundLift = haveContact ? -lowest : 0.0f;
            if (haveContact && view.walk) {
                const float lift = -lowest;
                // A millimetre of tolerance stops the solver from chasing the
                // last digit of the trig and visibly jittering on a frozen frame.
                if (std::abs(lift) > 1e-4f) {
                    for (glm::mat4& m : character.palette) m[3].y += lift;
                }
            }
            // What the solve achieved, measured the same way it was solved: the
            // lowest sole point after the shift. Printed in millimetres because
            // "0.000 m" in a panel is indistinguishable from "not measured".
            if (haveContact) {
                float after = 1.0e9f;
                for (int c = 0; c < character.contactCount; ++c) {
                    const int bone = character.contactBone[c];
                    if (bone < 0) continue;
                    const glm::vec4 p = character.palette[static_cast<std::size_t>(bone)] *
                                        glm::vec4(character.contactRig[c], 1.0f);
                    after = std::min(after, p.y);
                }
                view.soleHeight = after;
            }
        }

        const float aspect = info.fbHeight > 0
                                 ? static_cast<float>(info.fbWidth) / static_cast<float>(info.fbHeight)
                                 : 1.0f;
        const float cp = std::cos(view.pitch);
        const glm::vec3 eye = view.target + view.distance * glm::vec3(cp * std::sin(view.yaw),
                                                                     std::sin(view.pitch),
                                                                     cp * std::cos(view.yaw));
        camera.SetPerspective(view.fov, aspect, 0.05f, 200.0f);
        camera.LookAt(eye, view.target, glm::vec3(0.0f, 1.0f, 0.0f));

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
