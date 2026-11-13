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
#include <future>
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
enum class Action { Idle, Walk, Run, Crouch, Attack, Jump };

// ---------------------------------------------------------------------------
// The demo's own state around one character: everything that is NOT the rig
// itself. The rig (skeleton, meshes, maps, unit scale, rest worlds) is
// gldx::RiggedModel; the ground solve is gldx::GroundContact; the semantic bone
// lookup is gldx::RigProfile. What stays here is the scratch they write into
// and the walk-specific numbers derived from them.
// ---------------------------------------------------------------------------
struct Character {
    gldx::RiggedModel model;
    gldx::RigProfile  rig;
    gldx::GroundContact ground;

    std::vector<glm::mat4> pose;      // one local matrix per bone, rebuilt per frame
    std::vector<glm::mat4> palette;   // what the vertex shader reads as uBones[]
    GaitTuning tune;

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
    std::unique_ptr<gldx::ProceduralAttack> attackSource;
    std::unique_ptr<gldx::ProceduralJump>   jumpSource;
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

// Camera + the runtime state the pass, the HUD and the input callbacks share.
struct View {
    float yaw      = 0.62f;   // radians around the character, 0 = straight ahead (+Z)
    float pitch    = 0.17f;
    float distance = 3.6f;
    float fov      = 42.0f;
    float speed    = 1.30f;   // metres per second of floor travel
    // What the number keys asked for. Applied to the machine inside the frame loop,
    // never from a callback (see the OnKey comment).
    Action requestAction = Action::Walk;
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
            const bool useMaps = view_.maps;
            const bool hasNormal = useMaps && view_.normalMap && model.hasNormal();
            const gldx::Texture2D& albedo = useMaps ? model.AlbedoOrFallback() : model.fallbackAlbedo();
            const gldx::Texture2D& normal = hasNormal ? model.NormalOrFallback() : model.fallbackNormal();
            const gldx::Texture2D& metallic =
                useMaps ? model.MetallicOrFallback() : model.fallbackScalar();
            const gldx::Texture2D& roughness =
                useMaps ? model.RoughnessOrFallback() : model.fallbackScalar();
            albedo.Bind(0);
            normal.Bind(1);
            metallic.Bind(2);
            roughness.Bind(3);
            skin_->Set("uAlbedo", 0);
            skin_->Set("uNormal", 1);
            skin_->Set("uMetallic", 2);
            skin_->Set("uRoughness", 3);
            // A sampler bound to a fallback is still a sampler *sampled*; the flag
            // says whether the value it returns means anything, which is what the
            // shader branches on.
            skin_->Set("uHasAlbedo", (useMaps && model.hasAlbedo()) ? 1 : 0);
            skin_->Set("uHasNormal", hasNormal ? 1 : 0);
            skin_->Set("uHasMetallic", (useMaps && model.hasMetallic()) ? 1 : 0);
            skin_->Set("uHasRoughness", (useMaps && model.hasRoughness()) ? 1 : 0);

            const gldx::SkinnedMaterial material = model.PrimaryMaterial();
            skin_->Set("uBaseColor", material.baseColor);
            skin_->Set("uMetallicFactor", material.metallic);
            skin_->Set("uRoughnessFactor", material.roughness);

            for (const gldx::SkinnedMesh& mesh : model.meshes()) {
                mesh.Draw();
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
                case Action::Attack: actionName = "attack"; break;
                case Action::Jump:   actionName = "jump";   break;
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
            std::snprintf(line, sizeof(line), "ground: pelvis %+6.1f mm | lowest sole %+6.1f mm",
                          static_cast<double>(view_.groundLift * 1000.0f),
                          static_cast<double>(view_.soleHeight * 1000.0f));
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
                     bool reportGait) {
    // Classification has to happen BEFORE the upload, and it is the reason this
    // function is shaped the way it is. RiggedModel refuses to own the exporter
    // name table (two tables that disagree bind a rig halfway - see its file
    // comment), so the model cannot derive orientation or the sole plane itself;
    // both need RigProfile, which needs the Skeleton, which arrives inside `data`.
    //
    // So: classify the skeleton in place, measure the two numbers that depend on
    // the *measured* up axis, hand those to the model, and only then move the data.
    character.rig = gldx::RigProfile::Classify(data.skeleton);

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
    float bindLowestOnUp = 0.0f;
    bool firstVertex = true;
    for (const gldx::SkinnedMeshData& mesh : data.meshes) {
        for (const gldx::SkinnedVertex& v : mesh.vertices) {
            const float d = glm::dot(v.position, character.rig.up);
            bindLowestOnUp = firstVertex ? d : std::min(bindLowestOnUp, d);
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
        bindLowestOnUp);

    // One cycle = two steps, and one step lays down the horizontal distance the
    // foot covers between its extremes: 2 * legLength * sin(hipSwing). The floor
    // scroll and the cadence both come from this, which is what keeps the feet
    // from skating.
    character.cycleMetres =
        std::max(4.0f * model.legLengthMetres() * std::sin(character.tune.hipSwing), 0.1f);

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
    character.idleSource = std::make_unique<gldx::ProceduralIdle>(
        skeleton, character.rig, model.restWorld(), gldx::IdleTuning{});
    character.walkSource = std::make_unique<gldx::ProceduralWalk>(
        skeleton, character.rig, model.restWorld(), character.tune,
        character.cycleSeconds(1.0f));
    // The other four actions. Their durations are independent of the floor speed:
    // a run cycle is 0.72 s whatever the treadmill does (speed changes cadence in
    // this demo, and the run's own cadence is fixed), and jump/attack are events
    // with a fixed length.
    character.runSource = std::make_unique<gldx::ProceduralRun>(
        skeleton, character.rig, model.restWorld(), gldx::RunTuning{}, 0.72f);
    character.crouchSource = std::make_unique<gldx::ProceduralCrouch>(
        skeleton, character.rig, model.restWorld(), gldx::CrouchTuning{});
    character.attackSource = std::make_unique<gldx::ProceduralAttack>(
        skeleton, character.rig, model.restWorld(), gldx::AttackTuning{});
    character.jumpSource = std::make_unique<gldx::ProceduralJump>(
        skeleton, character.rig, model.restWorld(), gldx::JumpTuning{});
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
    }

    // Which rig slots failed to bind, and the one assertion this demo takes most
    // seriously: RigProfile keeps a missing bone as -1 and every rotation against
    // it is quietly a no-op, so a slot that failed to bind is invisible in the
    // picture (the limb just looks stiff) until this line names it. It used to be
    // hand-written here and lived through several review rounds with hand and
    // clavicle silently unbound.
    std::fprintf(stderr, "[anim-rigid] rig: %s\n", character.rig.BindingReport().c_str());
}

} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {"hud", "walk", "ground", "wireframe", "maps", "normalmap", "report"},
                                 {"model", "speed", "fov", "yaw", "pitch", "distance", "freeze-at",
                                  "target-y", "action"},
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

    // Which action to play. Exists so the non-walk actions can be verified without
    // a keyboard: the number keys are unreachable from a `--quit-after` run, and a
    // switch that can only be exercised by a human is a switch nobody regression
    // tests. Unknown names are ignored (the demo keeps walking) rather than fatal -
    // this is a demo, and a typo should not stop the window from opening.
    {
        const std::string action = flags.string("action", "walk");
        if (action == "idle")        view.requestAction = Action::Idle;
        else if (action == "walk")   view.requestAction = Action::Walk;
        else if (action == "run")    view.requestAction = Action::Run;
        else if (action == "crouch") view.requestAction = Action::Crouch;
        else if (action == "attack") view.requestAction = Action::Attack;
        else if (action == "jump")   view.requestAction = Action::Jump;
        else {
            std::fprintf(stderr, "[anim-rigid] unknown --action '%s'; using walk "
                                 "(known: idle, walk, run, crouch, attack, jump)\n",
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

    // The three dummies the character draws with when the file has no such map now
    // live inside RiggedModel (fallbackAlbedo/Normal/Scalar). They are the reason
    // a shader with an unbacked sampler cannot silently drop this draw on Apple.

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
            // Action selection. Only records the request; the frame loop applies it
            // to the machine, because switching actions is a per-frame decision
            // (it needs the current phase) and must not happen inside a GLFW
            // callback that can fire between two of our own updates.
            case gldx::win::Key::Num0: view.requestAction = Action::Idle; break;
            case gldx::win::Key::Num1: view.requestAction = Action::Walk; break;
            case gldx::win::Key::Num2: view.requestAction = Action::Run; break;
            case gldx::win::Key::Num3: view.requestAction = Action::Crouch; break;
            case gldx::win::Key::Num4: view.requestAction = Action::Attack; break;
            case gldx::win::Key::Num5: view.requestAction = Action::Jump; break;
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
            const gldx::RiggedModel& model = character.model;
            const float phase = character.phaseFor(view.speed, animSeconds);
            // Default path unchanged: Action::Walk samples the walk source at the
            // exact phase the pre-animation-layer demo used, so `--off hud
            // --freeze-at` frames stay byte-identical.
            const bool walking = (view.requestAction == Action::Walk);
            if (walking && view.walk && character.rig.walkable && character.walkSource) {
                character.walkSource->Sample(
                    character.walkTimeFor(phase), character.pose);
            } else if (walking) {
                gldx::ResetToRest(model.skeleton(), character.pose);
            } else if (!view.walk) {
                // W (walk on/off) is the master switch for the whole action layer:
                // with it off every action collapses to rest, which is what makes
                // `--off walk` a stable A/B baseline regardless of the number keys.
                gldx::ResetToRest(model.skeleton(), character.pose);
            } else {
                // The other four actions are time-driven rather than phase-driven.
                // Their clock is the same frozen-friendly `animSeconds`, so a
                // `--freeze-at` frame is still a pure function of argv.
                switch (view.requestAction) {
                    case Action::Idle:
                        if (character.idleSource)
                            character.idleSource->Sample(animSeconds, character.pose);
                        break;
                    case Action::Run:
                        if (character.runSource)
                            character.runSource->Sample(animSeconds, character.pose);
                        break;
                    case Action::Crouch:
                        if (character.crouchSource)
                            character.crouchSource->Sample(animSeconds, character.pose);
                        break;
                    case Action::Attack:
                        if (character.attackSource)
                            character.attackSource->Sample(animSeconds, character.pose);
                        break;
                    case Action::Jump:
                        if (character.jumpSource)
                            character.jumpSource->Sample(animSeconds, character.pose);
                        break;
                    case Action::Walk:
                        break;   // handled above
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
            const gldx::GroundContact::Solution solved = character.ground.Solve(character.palette);
            view.groundLift = solved.lift;
            if (solved.valid() && view.walk) {
                gldx::GroundContact::ApplyLift(character.palette, solved.lift);
            }
            // What the solve achieved, measured the same way it was solved: the
            // lowest sole point after the shift. Printed in millimetres because
            // "0.000 m" in a panel is indistinguishable from "not measured".
            view.soleHeight = solved.valid() ? solved.lowestAfter : 0.0f;
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
