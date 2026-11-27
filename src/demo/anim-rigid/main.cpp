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
#include "Character.h"
#include "ClothProbes.h"
#include "Probes.h"
#include "AssetBorrow.h"
#include "HudPass.h"
#include "Setup.h"
#include "SkinPass.h"
#include "Scene.h"
#include "View.h"

#include "Shaders.h"

using anim::Action;
using anim::AcesFilm;
using anim::Character;
using anim::ClothProbes;
using anim::ComputeGaitSwing;
using anim::DisplayColor;
using anim::Clamp01;
using anim::Ease01;
using anim::GaitSwing;
using anim::GaitTuning;
using anim::Segment01;
using anim::SkinPass;
using anim::UploadCharacter;
using anim::WalkHudPass;
using anim::BorrowMaterialMaps;
using anim::MakeSolidDesc;
using anim::kBounceColor;
using anim::BuildClothProbes;
using anim::ProbeHairTip;
using anim::ProbeLimbsAndHands;
using anim::ProbePenetration;
using anim::ProbeSkirtSymmetry;
using anim::ProbeSkirtTrace;
using anim::ProbeTwistCheck;
using anim::kGroundColor;
using anim::kPi;
using anim::kSkyColor;
using anim::kSunColor;
using anim::kSunDir;
using anim::kSunIntensity;
using anim::LegAngles;
using anim::LegAt;
using anim::NamingChannel;
using anim::View;

namespace {

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


// A clamp, an ease and a segment ramp - the three primitives the curves below
// are built from. Naming them keeps the gait readable as a gait instead of as
// arithmetic.

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
// Lowest skinned vertex of the character, in metres, from the decimated mesh copy.
//
// `feetOnly` restricts it to vertices weighted to the feet - right for a standing pose,
// wrong for a seated one whose support is the skirt.
[[nodiscard]] float LowestSkinnedY(const Character& c, const gldx::RiggedModel& model,
                                   bool feetOnly) {
    if (c.probeVerts.empty() || c.palette.empty()) return std::numeric_limits<float>::quiet_NaN();
    const int fL = c.rig.foot[gldx::kRigLeft], fR = c.rig.foot[gldx::kRigRight];
    const int tL = c.rig.toe[gldx::kRigLeft], tR = c.rig.toe[gldx::kRigRight];
    // Bones whose skin is allowed to define "the floor" for this pose.
    //
    // Measuring EVERY vertex is wrong for a seated pose: the hair and the tail hang
    // below the hips, so the character gets dragged down until a braid touches the
    // ground - measured, that buried it to the waist. The seat is the hips and the
    // skirt panels; those are the things that would actually rest on a floor.
    std::vector<int> allowed;
    if (feetOnly) {
        allowed = {fL, fR, tL, tR};
    } else if (c.rig.hips >= 0) {
        allowed = {c.rig.hips, c.rig.spine, c.rig.chest};
        for (int k = 0; k < c.rig.chains[static_cast<int>(gldx::RigChainKind::Skirt)].count; ++k)
            allowed.push_back(c.rig.chains[static_cast<int>(gldx::RigChainKind::Skirt)].bones[k]);
    }

    float low = 1.0e9f;
    for (const gldx::SkinnedVertex& v : c.probeVerts) {
        if (!allowed.empty()) {
            bool onFoot = false;
            for (int i = 0; i < 4; ++i) {
                if (v.boneWeight[i] <= 0.0f) continue;
                const int bi = static_cast<int>(v.boneIndex[i]);
                if (std::find(allowed.begin(), allowed.end(), bi) != allowed.end()) { onFoot = true; break; }
            }
            if (!onFoot) continue;
        }
        glm::vec4 q(0.0f);
        for (int i = 0; i < 4; ++i) {
            if (v.boneWeight[i] <= 0.0f) continue;
            const std::size_t bi = v.boneIndex[i];
            if (bi < c.palette.size())
                q += v.boneWeight[i] * (c.palette[bi] * glm::vec4(v.position, 1.0f));
        }
        low = std::min(low, q.y);
    }
    return low;
}


//
// While grounded the solve IS the answer: it puts the lowest sole on the floor and
// the pelvis height falls out of the leg angles. While airborne it means nothing -
// the soles are not supposed to be on the floor - so the flight height is measured
// from `restLift` (where straight legs stand) plus the source's own arc. The jump's
// take-off and touch-down frames are authored with straight legs, i.e. the rest pose,
// so at those two instants `restLift + 0 == solvedLift` and the branches meet with no
// visible step; that is the whole reason the arc is applied here and not inside the
// pose (the pose is in rig units, the arc is in metres).

}  // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {"hud", "walk", "ground", "wireframe", "maps", "normalmap", "report", "trace", "allparts", "twoside"},
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
    view.twoSided  = flags.on("twoside", false);
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
            BorrowMaterialMaps(modelPath, *parsed);
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
        // `GLDX_INPUT=1`: prove the event actually arrives. "The drag does nothing" has
        // three very different causes (no events / events ignored / camera not applied)
        // and reading the code cannot tell them apart. The synthetic-drag probe
        // (`GLDX_SIM_DRAG`) already proved the last one is fine, so this separates the
        // first two.
        if (std::getenv("GLDX_INPUT") && view.dragging) {
            static int n = 0;
            if (n++ % 10 == 0)
                std::fprintf(stderr, "[input] cursor pos=(%.1f,%.1f) dragging=1\n",
                             (double)pos.x, (double)pos.y);
        }
        if (!view.dragging) return;
        view.orbit.Drag(glm::vec2(static_cast<float>(pos.x - view.lastX),
                                  static_cast<float>(pos.y - view.lastY)));
        view.lastX = pos.x;
        view.lastY = pos.y;
    });
    window.OnMouseButton([&view](gldx::win::Window& w, gldx::win::MouseButton button,
                                 gldx::win::KeyAction action, int) {
        // Left OR right button orbits. Left is the documented control; right is here
        // because a trackpad drag can be ambiguous about which button the OS reports,
        // and "nothing happens" is a much worse failure than "two buttons work".
        if (button != gldx::win::MouseButton::Left &&
            button != gldx::win::MouseButton::Right) return;
        if (std::getenv("GLDX_INPUT"))
            std::fprintf(stderr, "[input] button=%d action=%d\n", static_cast<int>(button),
                         static_cast<int>(action));
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
            // Keyboard orbit as a fallback, so the camera is never unreachable when
            // the pointer path misbehaves on some window manager.
            case gldx::win::Key::Left:  view.orbit.Drag(glm::vec2(-40.0f, 0.0f)); break;
            case gldx::win::Key::Right: view.orbit.Drag(glm::vec2(40.0f, 0.0f)); break;
            case gldx::win::Key::Up:    view.orbit.Drag(glm::vec2(0.0f, -30.0f)); break;
            case gldx::win::Key::Down:  view.orbit.Drag(glm::vec2(0.0f, 30.0f)); break;
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
            // `3` used to bind Action::Crouch. Removed on request: the standalone crouch
            // pose is gone from the keyboard, `4` (crouch-walk) is the one that is still
            // reachable. The enum and the pose source stay - the crouch tuning is what
            // crouch-walk is built from, so deleting it would break action 4 as well.
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
        } else if (view.freezeAt > 0.0f) {
            // ---- a frozen frame is the ACTION, not a cross-fade in progress ----
            //
            // `dt` is derived from `animSeconds`, which `--freeze-at` clamps. So dt = 0
            // and the machine's blend weight can never advance: the pose stays frozen
            // at the FIRST blend sample, which is the previous action. Pressing sit (or
            // passing `--action sit`) therefore showed the character still standing -
            // measured: `current == sit`, `Sample` reporting thigh = -76.8 deg, and a
            // picture of a standing character, because the drawn pose was 100 % walk.
            //
            // Hard-cutting here is also the correct semantics: the caller asked for a
            // specific action's pose at a specific time, not for the transition into it.
            //
            // Only when it CHANGED: `Set` resets `currentTime_`, so calling it every
            // frame pins the source's clock at 0 and the drawn pose is always t=0 of the
            // action rather than the frozen time the caller asked for.
            if (character.machine.Current() != character.sourceFor(view.requestAction)) {
                character.machine.Set(character.sourceFor(view.requestAction));
            }
            character.machine.SetCurrentTime(animSeconds);
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

            // ---- 手指：所有动作共用一处，按动作给不同握持度 ----
            //
            // 手指不属于任何动作源的调参范围（它们没有摇晃物理，也没有碰撞），但少了
            // 这一段整条手臂就以"张开的巴掌"收尾——实测渲染里两只手是向外张开的扇子，
            // 走、跑、蹲走都一样。放在这里而不是各动作源里，是因为"手多松"是动作的
            // 属性，而"怎么弯"是骨架的属性，两者分开就不会出现某个动作忘了弯。
            {
                // 跑的时候是握拳，走路是放松的半握，游泳要张掌。
                float curl = 0.55f;
                float thumb = 0.30f;
                switch (view.requestAction) {
                    case Action::Idle:   curl = 0.45f; thumb = 0.24f; break;
                    case Action::Walk:   curl = 0.55f; thumb = 0.30f; break;
                    case Action::Run:    curl = 0.95f; thumb = 0.50f; break;
                    // Crouch and crouch-walk share one hand: they are the same action held
                    // at a phase, so the fingers must not change when the phase starts
                    // moving. Leaving `CrouchWalk` out of this switch was also the demo's
                    // only -Wswitch warning - it silently fell through to the walk value.
                    case Action::Crouch:
                    case Action::CrouchWalk: curl = 0.62f; thumb = 0.34f; break;
                    case Action::Sit:    curl = 0.45f; thumb = 0.24f; break;
                    case Action::Swim:   curl = 0.12f; thumb = 0.06f; break;
                    case Action::Attack: curl = 0.90f; thumb = 0.48f; break;
                    case Action::Jump:   curl = 0.70f; thumb = 0.38f; break;
                }
                if (const char* ov = std::getenv("GLDX_CURL"))
                    curl = static_cast<float>(std::atof(ov));
                if (const char* ov = std::getenv("GLDX_TCURL"))
                    thumb = static_cast<float>(std::atof(ov));
                // 弯曲轴是 side，不是 forward：绕 forward 转等于让手指沿自身长轴
                // 自转（画面完全不变，实测 curl 0 -> 2.5 只有 0 像素差异）。
                glm::vec3 curlAxis = character.rig.side;
                if (const char* ax = std::getenv("GLDX_CURLAXIS")) {
                    const char c = ax[0];
                    curlAxis = (c == 's') ? character.rig.side
                             : (c == 'u') ? character.rig.up
                                          : character.rig.forward;
                }
                gldx::CurlFingers(character.pose, character.model.restWorld(), character.rig,
                                  curlAxis, curl, thumb);
            }

            // ---- 把肘部旋转与位置都带给挂在上臂下的 `捩` 骨 ----
            //
            // MMD 手臂有两套 twist 骨，位置不同：
            //   左腕捩  -> 挂在 `左腕`(上臂) 下，是 `左ひじ` 的父级
            //   左手捩  -> 挂在 `左ひじ` 下（自动继承肘部，无需处理）
            //   右腕捩1 -> **直接挂 `右腕` 下**，是 `右腕捩` 的兄弟
            //
            // 手套/袖套网格的主导骨骼是 `左手捩` 与 `右腕捩1`（GLDX_MESHDOM 量得）。
            // pose 代码只转 `armUp`/`armLow`，所以挂在上臂下的 twist 骨跟着上臂走、却
            // 不跟着肘弯曲；而且它们的**原点在肩部**，旋转不会把它沿手臂移动。
            // 两件事叠加就是"袖套横在腰前、小臂从它中间穿过"。
            //
            // 先做一次世界快照，算完所有目标再统一应用：边算边改会让后一根骨的
            // `want` 被前一根的改动污染（实测同帧内前臂世界 y 从 1.283 漂到 1.181）。
            {
                std::vector<glm::mat4> wsnap;
                character.model.skeleton().ComputeWorld(character.pose, character.model.Mat4(),
                                                        wsnap);
                struct Move { int bone; glm::mat4 target; glm::mat4 invParent; };
                std::vector<Move> moves;
                for (int tb : character.twistBones) {
                    int sd = 0;
                    for (; sd < gldx::kRigSides; ++sd)
                        if (character.rig.armUp[sd] >= 0 &&
                            character.model.skeleton().bones()[(std::size_t)tb].parent ==
                                character.rig.armUp[sd])
                            break;
                    if (sd >= gldx::kRigSides) continue;
                    const int low = character.rig.armLow[sd];
                    if (low < 0) continue;

                    // 朝向：**直接取前臂的世界旋转**，不是复制局部增量。
                    //
                    // 复制增量只在这根骨的 rest 朝向与前臂一致时才对；这具资产里它们
                    // 不一致，于是袖套的开口朝上、没有包住小臂（位置对、朝向错）。
                    // 袖套是套在腕上的筒，它的轴向必须与前臂轴向一致，所以直接对齐。
                    const glm::mat4& foreRest =
                        character.model.skeleton().bones()[(std::size_t)low].restLocal;
                    (void)foreRest;

                    // 位置：把它挪到前臂关节处。`armLow` 是它的**兄弟**，旋转不改变
                    // 原点位置，所以必须显式平移过来（实测差 37 cm）。
                    const int par = character.model.skeleton().bones()[(std::size_t)tb].parent;
                    if (par < 0 || static_cast<std::size_t>(par) >= wsnap.size()) continue;
                    // Where it belongs: the posed forearm transformed by the bind-pose
                    // offset between them. Putting it at the elbow (the first attempt)
                    // left it 17 cm high, because the cuff's geometry sits part-way down
                    // the forearm - `左手捩` proves it by already hanging there.
                    const std::size_t idx =
                        static_cast<std::size_t>(&tb - character.twistBones.data());
                    const glm::vec3 bindOff =
                        (idx < character.twistForearmOffset.size())
                            ? character.twistForearmOffset[idx] : glm::vec3(0.0f);
                    const glm::mat4 foreWorld = wsnap[(std::size_t)low];
                    const glm::vec3 want =
                        glm::vec3(foreWorld * glm::vec4(bindOff, 1.0f));
                    // Build the world transform the cuff's bone SHOULD have: the forearm's
                    // rotation (so the cylinder's axis matches the arm) at the target
                    // position. Then convert to local via the parent's inverse.
                    glm::mat4 targetWorld = foreWorld;
                    targetWorld[3] = glm::vec4(want, 1.0f);
                    const glm::mat4 invParent = glm::inverse(wsnap[(std::size_t)par]);
                    moves.push_back({tb, targetWorld, invParent});
                }
                for (const Move& mv : moves)
                    character.pose[(std::size_t)mv.bone] = mv.invParent * mv.target;

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

            // ---- carry the elbow rotation onto twist bones hung under the upper arm ----
            //
            // `捩` bones that are children of `armUp` do not inherit the elbow: the pose
            // sources rotate `armLow`, and those bones are its siblings, not its
            // children. Gloves and cuffs are weighted to them, so without this the cuff
            // stays on the upper arm's line while the forearm swings away - measured as
            // "the cuff hangs beside the waist and the arm passes through it".
            //
            // Copying `armLow`'s local rotation is the correct amount: a twist bone is
            // the same segment of the arm, so it must bend with the same joint.
            for (int tb : character.twistBones) {
                int sd = 0;
                for (; sd < gldx::kRigSides; ++sd)
                    if (character.rig.armUp[sd] >= 0 &&
                        model.skeleton().bones()[(std::size_t)tb].parent == character.rig.armUp[sd])
                        break;
                if (sd >= gldx::kRigSides) continue;
                const int low = character.rig.armLow[sd];
                if (low < 0) continue;
                // Relative rotation of the elbow w.r.t. its parent, in the upper arm's
                // frame; applying it to the twist bone makes both bend together.
                const glm::mat4 rel = character.pose[(std::size_t)low];
                const glm::mat4 restLow = model.skeleton().bones()[(std::size_t)low].restLocal;
                const glm::mat4 delta = rel * glm::inverse(restLow);
                character.pose[(std::size_t)tb] =
                    model.skeleton().bones()[(std::size_t)tb].restLocal * delta;
                {
                    std::vector<glm::mat4> wcheck;
                    model.skeleton().ComputeWorld(character.pose, model.Mat4(), wcheck);
                    const glm::vec3 want = glm::vec3(wcheck[(std::size_t)low][3]);
                    const glm::vec3 have = glm::vec3(wcheck[(std::size_t)tb][3]);
                    // `TranslateBone` folds a WORLD offset into the bone's rest frame, but
                    // `have`/`want` are world positions AFTER the parent chain has moved.
                    // The parent of this twist bone is the upper arm, which is posed, so
                    // the offset has to be expressed in the bone's CURRENT frame - the
                    // rest frame is the wrong basis and the move lands somewhere else.
                    //
                    // Convert through the bone's current world rotation instead.
                    // The world matrices here are NOT pure rotations: they carry the
                    // rig's unit scale, so `transpose(mat3(world))` is not an inverse and
                    // shrinks a 0.1 m correction to 0.004. Convert through the parent's
                    // LOCAL-to-world instead: the offset the child needs in its own local
                    // space is the world delta expressed in the parent's frame, divided by
                    // the parent's scale.
                    const int par = model.skeleton().bones()[(std::size_t)tb].parent;
                    if (par < 0 || static_cast<std::size_t>(par) >= wcheck.size()) continue;
                    const glm::mat4 parentWorld =
                        model.Mat4() * wcheck[(std::size_t)par];
                    (void)parentWorld;
                    const glm::mat3 parBasis = glm::mat3(wcheck[(std::size_t)par]);
                    const float scale = glm::length(parBasis[0]);
                    if (!(scale > 1e-6f)) continue;
                    // World delta -> parent-local delta (rotation only, then unscale).
                    const glm::mat3 rot = parBasis / scale;
                    const glm::vec3 localOffset =
                        glm::transpose(rot) * (want - have) / scale;
                    character.pose[(std::size_t)tb][3] += glm::vec4(localOffset, 0.0f);
                }
            }
            ProbeTwistCheck(character);
        }

            // Real seconds since the previous frame, clamped: the first frame after the
            // model loads blocks for seconds, and integrating a spring over that whole
            // interval would fling every chain.
            // ---- secondary motion: stepped by the CLOCK, not by the wall clock ----
            //
            // The chains are the only stage that carries state across frames (the solver
            // integrates a spring from the previous frame's angular velocity), which
            // makes them the only place `--freeze-at` can break A/A byte equality: with
            // `info.dt` the frozen frame still depends on how many frames happened to
            // run before the freeze.
            //
            // The first fix was `dt = 0` when frozen. That did make A/A exact, and it
            // also froze the cloth, hair and tail SOLID - measured against
            // `GLDX_NO_SECONDARY`: 0 differing pixels at t=0.5 and t=0.8, i.e. the
            // exact "clothes do not move" failure this whole layer exists to avoid. A
            // regression test that passes by disabling the feature is not a pass.
            //
            // The honest fix is a FIXED step tied to the same clock the pose uses: the
            // chains advance by `kSecondaryStep` per frame while the clock runs, so
            // their state is a function of elapsed time rather than of frame count.
            // Freezing then means "stop advancing", and the accumulated state at the
            // frozen instant is the same on every run.
            constexpr float kSecondaryStep = 1.0f / 60.0f;
            // While the clock runs: advance by the real frame time, capped so a stall
            // cannot fling the springs. While frozen: keep advancing at the FIXED step,
            // because "frozen" means the POSE stopped, not that the cloth stopped.
            //
            // A frozen pose is a steady state, and the chains must be given time to
            // settle INTO it - a skirt that was mid-swing when the clock stopped would
            // otherwise be photographed mid-flight and every frozen frame would show a
            // different amount of fabric lag. Settling at a fixed step makes the frozen
            // picture a property of "how long has it been frozen", and the snapshot is
            // taken at a fixed GLDX_SNAPSHOT_AT, so the answer is the same every run.
            // ---- frozen means: settle the cloth ONCE, then stop ----
            //
            // Writing the obvious "keep advancing with a fixed step" trades one bug for
            // another: the step count is frames, the frame count is wall-clock, and
            // three runs produced three different md5s. The chain state has to become a
            // function of the frozen INSTANT, not of how long the process kept drawing.
            //
            // So the settle happens exactly once, the first frame after the clock stops:
            // run the solver for a fixed `kSettleSteps` at the fixed step, which is
            // enough for these spring constants to reach steady state (stiffness 0.22-0.30
            // per step ⇒ under 1 % of the initial offset remains after ~1 s of steps).
            // Every later frame passes dt = 0, which is a no-op for the solver.
            constexpr int kSettleSteps = 600;
            // ---- 结算是"冻结姿态"的纯函数，所以必须等时钟**真的**停住 ----
            //
            // 这里原来判的是 `view.freezeAt > 0.0f`，那是"命令行要求冻结"，不是
            // "时钟已经停住"——第一帧就满足它。而第一帧的 `info.time` 是从 Run() 开始
            // 到这一帧帧首的墙钟，每次运行都不同（窗口/GL 上下文/字体初始化的耗时，
            // 实测 0.19~0.26 s，加载模型那次阻塞不在里面，因为 info.time 在 onFrame
            // 之前就采好了）。于是：
            //
            //   · 结算被喂进 t≈0.2 s 的姿态，
            //   · 快照却在 t=1.0 s 的姿态上画出来，
            //
            // 既不是同一个姿态，两次运行也不一样——这正是"同一个二进制、同样 argv、
            // 逐字节不同"的来源（实测 46 万字节差异、settlehash 5.108/4.884/4.913）。
            // 复位弹簧、改呼吸时钟都治不了它，因为结算的**输入**每次就差一点。
            //
            // 判据只能是"时钟被钳住了"：`view.clock` 是 `min(info.time, freezeAt)`，
            // 只有 info.time 越过 freezeAt 之后它才恒等于 freezeAt。
            const bool clockFrozen =
                view.freezeAt > 0.0f && view.clock >= static_cast<double>(view.freezeAt);
            if (clockFrozen) {
                if (!character.secondarySettled) {
                    character.secondarySettled = true;
                    character.secondarySettleSteps = kSettleSteps;
                }
                // Spend the WHOLE budget in this one frame. Advancing one step per
                // frame made "has the cloth settled yet" a function of the frame rate
                // (measured: three runs, three different md5s), because the snapshot
                // arrives after a variable number of frames.
                if (character.secondarySettleSteps > 0) {
                    // 结算前把弹簧状态复位到静止。
                    //
                    // 不复位时，结算的**起点**是"冻结前跑了多少帧"的残留：那个帧数
                    // 由墙上时钟决定，每次运行都不同。600 步只能压到 1e-3 量级，
                    // 而 2.2 m 视距下 1 mm 正好是一像素——于是同一个 --freeze-at
                    // 快照两次跑出上万像素差异（实测 1697~14803）。复位之后结算
                    // 成为"冻结姿态"的纯函数，与跑了几帧无关。
                    if (!std::getenv("GLDX_NO_RESET_SETTLE")) {
                    for (int ci = 0; ci < gldx::RigChainKindCount; ++ci) {
                        if (!character.rig.hasChain(static_cast<gldx::RigChainKind>(ci))) continue;
                        character.chainSolvers[ci].Reset(character.rig.chains[ci]);
                    }
                    }
                    // 每一步都从**冻结姿态**重新施加角度，而不是在上一步的结果上继续
                    // 右乘。`Solve` 的写回是 `local[bone] *= rot`，600 步连着叠的
                    // 话世界矩阵已经被转过 600 次，而结算循环里的探针、摆腿信号、
                    // `tuck` 的侧向符号全都在读那个世界——量出来的目标不是冻结姿态的。
                    const std::vector<glm::mat4> settleBase = character.pose;
                    for (int si = 0; si < character.secondarySettleSteps; ++si) {
                        character.pose = settleBase;
                        std::vector<glm::mat4> smWorldSettle;
                        model.skeleton().ComputeWorld(character.pose, model.Mat4(), smWorldSettle);
                        for (int ci = 0; ci < gldx::RigChainKindCount; ++ci) {
                            const auto kind = static_cast<gldx::RigChainKind>(ci);
                            if (!character.rig.hasChain(kind)) continue;
                            if (std::getenv("GLDX_NO_SECONDARY")) continue;
                            // 摆腿信号必须按**结算中的姿态**重算。复用上一活动帧留下的
                            // 值会让同一个 --freeze-at 快照两次跑出不同的裙摆：那个帧的
                            // 相位每次运行都可能不同（实测 1697~14803 像素差异）。
                            if (kind == gldx::RigChainKind::Skirt) {
                                // 探针也按结算姿态重建：否则结算是"冻结姿态 + 上一活动帧
                                // 的探针"，结果仍取决于冻结前跑了多少帧。
                                const ClothProbes sp = BuildClothProbes(character, smWorldSettle);
                                character.chainSolvers[ci].SetLegProbes(sp.legs, sp.legRadius);
                                for (std::size_t pi = 0; pi < sp.legRadii.size(); ++pi)
                                    character.chainSolvers[ci].AddProbes({sp.legs[pi]}, sp.legRadii[pi]);
                                character.chainSolvers[ci].AddProbes(sp.hands, 0.048f);
                                for (std::size_t pi = 0; pi < sp.armMids.size(); ++pi)
                                    character.chainSolvers[ci].AddProbes({sp.armMids[pi]}, 0.045f);
                                const GaitSwing sw = ComputeGaitSwing(character, smWorldSettle);
                                const bool gaitActive =
                                    view.requestAction == Action::Walk ||
                                    view.requestAction == Action::Run ||
                                    view.requestAction == Action::CrouchWalk;
                                character.chainTune[ci].gaitSwingL = gaitActive ? sw.left : 0.0f;
                                character.chainTune[ci].gaitSwingR = gaitActive ? sw.right : 0.0f;
                            }
                            character.chainSolvers[ci].SetClock(static_cast<float>(view.clock));
                            character.chainSolvers[ci].Solve(
                                character.rig.chains[ci], character.pose, smWorldSettle,
                                model.restWorld(), character.chainTune[ci], kSecondaryStep);
                        }
                    }
                    // 结算结束时 pose 里是"冻结姿态 + 最后一次角度"；下面那段常规
                    // 二次运动还会再施加一次同样的角度（dt=0 只跳过积分、不跳过写回）。
                    // 不还原就等于把同一份旋转乘两遍，裙摆比结算出来的更外翻。
                    character.pose = settleBase;
                    character.secondarySettleSteps = 0;
                    if (std::getenv("GLDX_SETTLEHASH")) {
                        // 结算结果的指纹：哪条链的哪个数字在两次运行之间不同，
                        // 就指向那一层，不必再猜。
                        double acc = 0.0;
                        for (int ci = 0; ci < gldx::RigChainKindCount; ++ci) {
                            if (!character.rig.hasChain(static_cast<gldx::RigChainKind>(ci)))
                                continue;
                            acc += character.chainSolvers[ci].StateFingerprint();
                        }
                        std::fprintf(stderr, "[settlehash] %.9f\n", acc);
                    }
                }
                character.secondaryDt = 0.0f;
            } else {
                character.secondarySettled = false;
                character.secondarySettleSteps = 0;
                character.secondaryDt =
                    std::min(std::clamp(static_cast<float>(info.dt), 0.0f, 0.05f), kSecondaryStep);
            }

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
                const ClothProbes probes = BuildClothProbes(character, smWorld);
                std::vector<glm::vec3> legPts = probes.legs;
                std::vector<float> legRads = probes.legRadii;
                const std::vector<glm::vec3>& handPts = probes.hands;
                const std::vector<glm::vec3>& armMidPts = probes.armMids;
                const float legRadius = probes.legRadius;

                // ---- 两侧大腿的前后摆幅（膝盖相对髋的前向位移，米）----
                //
                // 这是"走路时裙摆要动"的信号源。髋关节在这个原地走的 demo 里不平移，
                // 所以按探针邻近度加权得到的驱动量恒为 0（实测 0.000）；膝盖的位移
                // 才是真正在变的量。
                {
                    const glm::mat4 hipM = smWorld[(std::size_t)character.rig.hips];
                    const glm::vec3 fwdW = glm::normalize(glm::vec3(hipM[2]) * -1.0f +
                                                          glm::vec3(0.0f));   // 恒等于朝前
                    (void)fwdW;
                    // 用骨架的 restWorld 前向做参考：模型正面朝 -Z（与相机约定一致）。
                    // 只有步态动作才下发摆腿信号。
                    //
                    // 坐姿/跪姿里膝盖是**前折**的，`knee - hip` 的前向分量很大（0.2~0.3 m），
                    // 若照样下发，每片裙摆会被恒定推 0.6 rad 外翻，整条裙子翻成一个向上
                    // 开的碗（用户截图就是这个）。摆动是步态的特征，不是姿态的特征。
                    const bool gaitActive =
                        view.requestAction == Action::Walk ||
                        view.requestAction == Action::Run ||
                        view.requestAction == Action::CrouchWalk;
                    const GaitSwing swing = ComputeGaitSwing(character, smWorld);
                    character.gaitSwingL = gaitActive ? swing.left : 0.0f;
                    character.gaitSwingR = gaitActive ? swing.right : 0.0f;
                    if (std::getenv("GLDX_GAITDBG")) {
                        static int n = 0;
                        if (n++ % 12 == 0)
                            std::fprintf(stderr, "[gait] L=%+.4f R=%+.4f\n",
                                         (double)character.gaitSwingL,
                                         (double)character.gaitSwingR);
                    }
                }
                for (int ci = 0; ci < gldx::RigChainKindCount; ++ci) {
                    const auto kind = static_cast<gldx::RigChainKind>(ci);
                    if (!character.rig.hasChain(kind)) continue;
                    if (std::getenv("GLDX_NO_SECONDARY")) continue;   // A/B switch
                    // Only the skirt has legs inside it.
                    if (kind == gldx::RigChainKind::Skirt) {
                        character.chainSolvers[ci].SetLegProbes(legPts, legRadius);
                        for (std::size_t pi = 0; pi < legRads.size(); ++pi)
                            character.chainSolvers[ci].AddProbes({legPts[pi]}, legRads[pi]);
                        // 手用小半径，而且**肩到肘**也要有探针：手往后摆时穿的是小臂，
                        // 不是手腕那一点。
                        character.chainSolvers[ci].AddProbes(handPts, 0.048f);
                        for (std::size_t pi = 0; pi < armMidPts.size(); ++pi)
                            character.chainSolvers[ci].AddProbes({armMidPts[pi]}, 0.045f);
                    }
                    // 裙摆：把这一帧的摆腿信号下发（按左右分别给）。
                    if (kind == gldx::RigChainKind::Skirt) {
                        character.chainTune[ci].gaitSwingL = character.gaitSwingL;
                        character.chainTune[ci].gaitSwingR = character.gaitSwingR;
                    }
                    if (std::getenv("GLDX_GAITDBG") && kind == gldx::RigChainKind::Skirt) {
                        static int n3 = 0;
                        if (n3++ % 12 == 0)
                            std::fprintf(stderr, "[tune] skirt count=%d gd=%.2f swL=%+.3f swR=%+.3f\n",
                                         character.rig.chains[ci].count,
                                         (double)character.chainTune[ci].gaitDrive,
                                         (double)character.chainTune[ci].gaitSwingL,
                                         (double)character.chainTune[ci].gaitSwingR);
                    }
                    // 呼吸相位跟着动画时钟走，不跟帧数走（见 ChainSolver::SetClock）。
                    character.chainSolvers[ci].SetClock(static_cast<float>(view.clock));
                    const gldx::ChainTuning& tk = character.chainTune[ci];
                    character.chainSolvers[ci].Solve(character.rig.chains[ci], character.pose,
                                                     smWorld, model.restWorld(), tk,
                                                     character.secondaryDt);
                }
                // Burn one step of the frozen settle budget per frame.
                //
                // NOTE: the budget must be spent INSIDE one frame (see the loop above);
                // spreading it across frames makes the settle finish depend on the frame
                // rate, which is the same non-determinism this whole mechanism exists to
                // remove.
                if (character.secondarySettleSteps > 0) --character.secondarySettleSteps;
            }

            // ---- lower the whole body for poses that fold the legs ----
            //
            // Rotating the knees cannot lower the pelvis: the pose sources write joint
            // angles, and on MMD rigs the legs (`下半身`) and the torso (`上半身`) are
            // SIBLINGS under `腰`. So a crouch came out as "standing with bent knees"
            // (head 1.689 m vs idle 1.702 m) and a sit stayed at standing height.
            //
            // Translating the common ancestor is the one place that moves both halves.
            // The amount comes from the geometry: how much shorter the hip-to-ankle
            // distance is once the legs fold.
            if (character.bodyRoot >= 0) {
                float drop = 0.0f;
                if (view.requestAction == Action::Crouch ||
                    view.requestAction == Action::CrouchWalk) {
                    const float hipA = character.tune.crouchHip;
                    const float kneeA = character.tune.crouchKnee;
                    // Thigh / shin shares of the leg; shin sees the SUM of both angles
                    // because it inherits the hip rotation.
                    constexpr float kThigh = 0.48f, kShin = 0.52f;
                    const float folded = kThigh * std::cos(hipA) +
                                         kShin * std::cos(hipA - kneeA);
                    drop = std::max(1.0f - folded, 0.0f) * model.legLengthMetres();
                } else if (view.requestAction == Action::Sit) {
                    drop = -character.sitRise;   // authored seated height, already tuned
                }
                if (drop > 1e-4f) {
                    gldx::TranslateBone(character.pose, model.restWorld(), character.bodyRoot,
                                        -character.rig.up * drop);
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

            // ---- 用"即将上传的调色板"把角色贴到地面 ----
            //
            // 在此之前所有高度修正都读的是另一个时刻的 palette，于是报告说 `sole
            // 0.000` 而画面悬空。这里改成：算完 palette、在它被上传之前，直接量它的
            // 最低顶点，再把它整体下移。量什么就改什么，中间没有第二个数据源。
            //
            // 只对"应该踩在地上"的动作生效：jump 的腾空高度是作者给的，run 有腾空
            // 段，把它们拉下来会把跳跃删掉。
            if (view.walk && !character.probeVerts.empty()) {
                float low = 1.0e9f;
                for (const gldx::SkinnedVertex& v : character.probeVerts) {
                    glm::vec4 q(0.0f);
                    for (int i = 0; i < 4; ++i) {
                        if (v.boneWeight[i] <= 0.0f) continue;
                        const std::size_t bi = v.boneIndex[i];
                        if (bi < character.palette.size())
                            q += v.boneWeight[i] *
                                 (character.palette[bi] * glm::vec4(v.position, 1.0f));
                    }
                    low = std::min(low, q.y);
                }
                // Run is NOT in this list any more. It used to be, which meant it got
                // no vertical correction at all and hovered 0.135 m up for the whole
                // cycle. It is handled by a constant offset below instead: per-frame
                // grounding would flatten its flight phase, and "no correction" is the
                // hover. The offset is the cycle minimum, so the lowest instant of the
                // cycle is what plants on the floor.
                const bool airborne = (view.requestAction == Action::Jump) ||
                                      (view.requestAction == Action::Swim);
                if (view.requestAction == Action::Run && character.runGroundReady) {
                    gldx::GroundContact::ApplyLift(character.palette,
                                                   character.runGroundOffset);
                }
                // Which vertices may define "the floor" for this pose.
                //
                // For a seated pose it must be the SEAT, not "whatever hangs lowest".
                // Measured: the lowest vertex was `右リボン大２` - a ribbon dangling from
                // the costume - so the grounding pass planted the RIBBON on the floor and
                // left the hips ~20 cm up. A ribbon is not what a person sits on.
                const bool seated = (view.requestAction == Action::Sit);
                // Iterate: skinning is not a rigid translation, so moving every bone
                // by -d does not move the lowest vertex by exactly -d when that vertex
                // is blended across bones. One pass measured crouch still 20 cm high.
                // A seated pose folds the thighs up through the skirt, so the hem has to
                // move out of the way. Lifting the skirt CHAIN bones (not the skirt mesh)
                // is the one lever that works without re-authoring the costume: the
                // palette is already built here, and translating a chain bone drags its
                // panels - and the ribbons hanging off them - up with it.
                //
                // Amount is measured against the poke-through, not guessed: see the
                // `--action sit` front view.
                // 步态时下摆略微"骑上去"。
                //
                // 短裙 + 摆腿：大腿一旦摆过下摆边缘，布面再怎么被推开也挡不住那条腿
                // 切过下摆线——真实的短裙是靠下摆**沿腿滑上去**的。给一个很小的抬升
                // 与外扩，让下摆贴着大腿走，而不是停在原地被切开。
                if (!seated &&
                    (view.requestAction == Action::Walk ||
                     view.requestAction == Action::Run ||
                     view.requestAction == Action::CrouchWalk)) {
                    float gaitLift = 0.020f;
                    if (const char* ov = std::getenv("GLDX_GAITLIFT"))
                        gaitLift = static_cast<float>(std::atof(ov));
                    const int ks = static_cast<int>(gldx::RigChainKind::Skirt);
                    const int kc = character.rig.chains[ks].count;
                    const glm::vec3 hipP(character.palette[(std::size_t)character.rig.hips][3]);
                    for (int i = 0; i < kc; ++i) {
                        const int b = character.rig.chains[ks].bones[i];
                        if (b < 0 || static_cast<std::size_t>(b) >= character.palette.size())
                            continue;
                        glm::mat4& m = character.palette[static_cast<std::size_t>(b)];
                        m[3][1] += gaitLift;
                        glm::vec3 rad(m[3][0] - hipP.x, 0.0f, m[3][2] - hipP.z);
                        const float len = glm::length(rad);
                        if (len > 1.0e-4f) {
                            rad /= len;
                            m[3][0] += rad.x * 0.012f;
                            m[3][2] += rad.z * 0.012f;
                        }
                    }
                }
                if (seated) {
                    float skirtLift = 0.045f;
                    if (const char* ov = std::getenv("GLDX_SKIRTLIFT"))
                        skirtLift = static_cast<float>(std::atof(ov));
                    float skirtFlare = 0.200f;
                    if (const char* ov = std::getenv("GLDX_SKIRTFLARE"))
                        skirtFlare = static_cast<float>(std::atof(ov));
                    const int ks = static_cast<int>(gldx::RigChainKind::Skirt);
                    const int kc = character.rig.chains[ks].count;
                    const glm::vec3 hipP(character.palette[(std::size_t)character.rig.hips][3]);
                    for (int i = 0; i < kc; ++i) {
                        const int b = character.rig.chains[ks].bones[i];
                        if (b < 0 || static_cast<std::size_t>(b) >= character.palette.size())
                            continue;
                        glm::mat4& m = character.palette[static_cast<std::size_t>(b)];
                        m[3][1] += skirtLift;
                        // Push the panel OUTWARD along the radial (horizontal) direction,
                        // so the hem drapes around the folded thighs instead of the knees
                        // pushing through it. Lifting alone only trades clipping for a
                        // skirt that looks hoisted off the waist.
                        glm::vec3 rad(m[3][0] - hipP.x, 0.0f, m[3][2] - hipP.z);
                        const float len = glm::length(rad);
                        if (len > 1.0e-4f) {
                            rad /= len;
                            m[3][0] += rad.x * skirtFlare;
                            m[3][2] += rad.z * skirtFlare;
                        }
                    }
                }
                // For a seated pose the support is BODY SKIN: the buttocks, the backs of
                // the thighs and the shins. It is NOT:
                //   - the hair/tail/skirt chains (they hang below and would be planted on
                //     the floor instead - measured: the lowest vertex was the ribbon bone
                //     `右リボン大２`, which left the hips hovering ~20 cm up), nor
                //   - a hand-picked bone list containing only `hips` (its vertices sit
                //     inside the pelvis, far above the skin that actually touches).
                //
                // So: everything EXCEPT the secondary-motion chains.
                std::vector<int> chainBones;
                if (seated) {
                    for (int k = 0; k < gldx::RigChainKindCount; ++k) {
                        const auto& ch = character.rig.chains[k];
                        for (int t = 0; t < ch.count; ++t) chainBones.push_back(ch.bones[t]);
                    }
                }
                for (int pass = 0; !airborne && pass < 4; ++pass) {
                    float now = 1.0e9f;
                    for (const gldx::SkinnedVertex& v : character.probeVerts) {
                        if (seated) {
                            // Skip vertices whose DOMINANT bone belongs to a chain.
                            int dom = -1;
                            float bestW = 0.0f;
                            for (int i = 0; i < 4; ++i) {
                                if (v.boneWeight[i] > bestW) {
                                    bestW = v.boneWeight[i];
                                    dom = static_cast<int>(v.boneIndex[i]);
                                }
                            }
                            if (dom >= 0 &&
                                std::find(chainBones.begin(), chainBones.end(), dom) !=
                                    chainBones.end())
                                continue;
                        }
                        glm::vec4 q(0.0f);
                        for (int i = 0; i < 4; ++i) {
                            if (v.boneWeight[i] <= 0.0f) continue;
                            const std::size_t bi = v.boneIndex[i];
                            if (bi < character.palette.size())
                                q += v.boneWeight[i] *
                                     (character.palette[bi] * glm::vec4(v.position, 1.0f));
                        }
                        now = std::min(now, q.y);
                    }
                    if (!std::isfinite(now) || std::abs(now) < 1e-4f) break;
                    gldx::GroundContact::ApplyLift(character.palette, -now);
                }
                (void)low;
            }
            }
            // Diagnostics (all env-gated, all pure readers of the finished pose) live in
            // Probes.cpp so the functional code around them - finger curl, twist bones,
            // seated lift, ground solve - is not interleaved with measurement.
            ProbePenetration(character, view);
            ProbeSkirtTrace(character, view);
            ProbeSkirtSymmetry(character);
            ProbeHairTip(character);
            ProbeLimbsAndHands(character, view);
            // Where the lowest sole ended up after the shift: the number that says
            // "planted" (0) or "in the air" (>0). Printed in millimetres because
            // "0.000 m" in a panel is indistinguishable from "not measured".
            view.soleHeight = solved.valid() ? (-solved.lift + shift) : 0.0f;
        }

        const float aspect = info.fbHeight > 0
                                 ? static_cast<float>(info.fbWidth) / static_cast<float>(info.fbHeight)
                                 : 1.0f;
        // 拖拽的兜底：直接查按键状态，不信按下事件。
        //
        // 按下事件只有一条路径能到达（OnMouseButton）。若它因为窗口焦点、触控板
        // 手势归属等原因没送到，`view.dragging` 永远是 false，光标回调里第一句就
        // return——表现正是"按住拖，视角不动"。这里每帧查一次物理按键状态，按下
        // 事件丢了也照样能转。
        if (!view.dragging &&
            (window.MouseIsDown(gldx::win::MouseButton::Left) ||
             window.MouseIsDown(gldx::win::MouseButton::Right))) {
            view.dragging = true;
            const gldx::win::Vec2d c = window.CursorPos();
            view.lastX = c.x;
            view.lastY = c.y;
        } else if (view.dragging &&
                   !window.MouseIsDown(gldx::win::MouseButton::Left) &&
                   !window.MouseIsDown(gldx::win::MouseButton::Right)) {
            // 松开事件丢了也要能停下来，否则光标一动就转。
            view.dragging = false;
        }
        // `GLDX_SIM_DRAG=1`: synthesise the exact call the mouse callback would make,
        // bypassing event delivery. If the picture does NOT change with this on, the
        // fault is in the render path (Apply/settings); if it DOES change, the fault is
        // that the cursor/button events never arrive. Reading the code cannot tell those
        // two apart, and they need completely different fixes.
        if (std::getenv("GLDX_SIM_DRAG")) {
            static int simFrames = 0;
            if (++simFrames == 20) {
                view.orbit.Drag(glm::vec2(120.0f, 0.0f));
                std::fprintf(stderr, "[simdrag] yaw now %.4f\n",
                             (double)view.orbit.settings().yaw);
            }
        }
        // `GLDX_INPUT=1` prints every event the window library actually delivers, so
        // "the drag does nothing" can be split into "no events" vs "events ignored".
        if (std::getenv("GLDX_INPUT")) {
            std::fprintf(stderr, "[input] frame=%d dragging=%d yaw=%.4f\n", view.drawCalls,
                         view.dragging ? 1 : 0, (double)view.orbit.settings().yaw);
        }
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
