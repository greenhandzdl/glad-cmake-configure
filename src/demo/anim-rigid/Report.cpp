// Module first, then the headers that name its types.
import gldx;

#include "Report.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace anim {

void ReportGait(Character& character, const char* traceAction) {
    const gldx::RiggedModel& model = character.model;
    const gldx::Skeleton& skeleton = model.skeleton();
    const gldx::RigProfile& rig = character.rig;
        // Numbers, not vibes: the two things a hand-authored walk gets wrong are
        // (a) the swing foot not clearing the floor and (b) the planted foot
        // drifting up off it. Both are measurable here without a single pixel.
        std::fprintf(stderr, "[anim-rigid] gait: phase  hipL   kneeL  ankleL | soleL    soleR\n");
        std::vector<glm::mat4> pose;
        std::vector<glm::mat4> palette;
        constexpr int kSamples = 32;
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
    if (std::getenv("GLDX_AXES")) {
        const auto& r = character.rig;
        const float ds = glm::dot(r.side, r.forward);
        const float du = glm::dot(r.up, r.forward);
        const float dsu = glm::dot(r.side, r.up);
        std::fprintf(stderr,
                     "[axes] side=(%.3f,%.3f,%.3f) |%.3f|  forward=(%.3f,%.3f,%.3f) |%.3f|  "
                     "up=(%.3f,%.3f,%.3f) |%.3f|\n"
                     "[axes] orthogonality: side.forward=%+.3f up.forward=%+.3f side.up=%+.3f "
                     "  (all must be ~0; lengths ~1)\n",
                     (double)r.side.x, (double)r.side.y, (double)r.side.z,
                     (double)glm::length(r.side), (double)r.forward.x, (double)r.forward.y,
                     (double)r.forward.z, (double)glm::length(r.forward), (double)r.up.x,
                     (double)r.up.y, (double)r.up.z, (double)glm::length(r.up), (double)ds,
                     (double)du, (double)dsu);
    }

    if (std::getenv("GLDX_SWIMKICK") && character.swimSource) {
        const auto& sk7 = model.skeleton();
        // Sample at 1/8 intervals: `sin` is ZERO at 0, 0.25, 0.5, 0.75 of the kick
        // cycle, so a quarter-period probe reports "the legs never move" for a leg that
        // is moving perfectly well.
        for (float ph : {0.0f, 0.125f, 0.25f, 0.375f, 0.5f}) {
            std::vector<glm::mat4> pose, wm;
            character.swimSource->Sample(ph * character.swimSource->Duration(), pose);
            sk7.ComputeWorld(pose, model.Mat4(), wm);
            auto P = [&](int b) { return b < 0 ? glm::vec3(0) : glm::vec3(wm[(std::size_t)b][3]); };
            const glm::vec3 lf = P(character.rig.foot[0]) - P(character.rig.hips);
            const glm::vec3 rf = P(character.rig.foot[1]) - P(character.rig.hips);
            const glm::vec3 thL = P(character.rig.legLow[0]) - P(character.rig.legUp[0]);
            const glm::vec3 thR = P(character.rig.legLow[1]) - P(character.rig.legUp[1]);
            std::fprintf(stderr,
                         "[kick] ph=%.2f footL.up=%+.3f footR.up=%+.3f | thighL.up=%+.3f thighR.up=%+.3f\n",
                         (double)ph, (double)lf.y, (double)rf.y,
                         (double)glm::normalize(thL).y, (double)glm::normalize(thR).y);
        }
    }
    // `GLDX_GAITLOW=run|walk|crouchwalk`: sweep one gait's whole cycle and report the
    // lowest skinned vertex per phase. A gait cannot be grounded per frame without
    // flattening its flight phase, so what it needs is a CONSTANT offset = the cycle
    // minimum; this prints exactly that number.
    // Run needs a constant ground offset; compute it once, from the same measurement
    // the probe below prints. Doing it here (after the probe vertices exist) keeps the
    // number derived from the geometry instead of hard-coded.
    if (!character.runGroundReady && character.runSource && !character.probeVerts.empty()) {
        const float dur = character.runSource->Duration();
        float lowest = 1.0e9f;
        for (int q = 0; q < 24; ++q) {
            std::vector<glm::mat4> pose, pal;
            character.runSource->Sample(dur * (float)q / 24.0f, pose);
            character.model.skeleton().ComputeSkinning(pose, character.model.Mat4(), pal);
            for (const gldx::SkinnedVertex& v : character.probeVerts) {
                glm::vec4 wp(0.0f);
                for (int i = 0; i < 4; ++i) {
                    if (v.boneWeight[i] <= 0.0f) continue;
                    const std::size_t bi = v.boneIndex[i];
                    if (bi < pal.size())
                        wp += v.boneWeight[i] * (pal[bi] * glm::vec4(v.position, 1.0f));
                }
                lowest = std::min(lowest, wp.y);
            }
        }
        if (std::isfinite(lowest) && lowest < 1.0e8f) {
            character.runGroundOffset = -lowest;
            character.runGroundReady  = true;
            std::fprintf(stderr,
                         "[run-ground] cycle minimum %.4f m -> constant lift %.4f m\n",
                         (double)lowest, (double)character.runGroundOffset);
        }
    }
    if (const char* which = std::getenv("GLDX_GAITLOW")) {
        gldx::AnimPoseSource* src = nullptr;
        if (std::strcmp(which, "run") == 0) src = character.runSource.get();
        else if (std::strcmp(which, "walk") == 0) src = character.walkSource.get();
        else if (std::strcmp(which, "crouchwalk") == 0) src = character.crouchWalkSource.get();
        if (src != nullptr && !character.probeVerts.empty()) {
            const float dur = src->Duration();
            float best = 1.0e9f;
            float worst = -1.0e9f;
            for (int q = 0; q < 24; ++q) {
                std::vector<glm::mat4> pose, pal;
                src->Sample(dur * (float)q / 24.0f, pose);
                character.model.skeleton().ComputeSkinning(pose, character.model.Mat4(), pal);
                float low = 1.0e9f;
                for (const gldx::SkinnedVertex& v : character.probeVerts) {
                    glm::vec4 wp(0.0f);
                    for (int i = 0; i < 4; ++i) {
                        if (v.boneWeight[i] <= 0.0f) continue;
                        const std::size_t bi = v.boneIndex[i];
                        if (bi < pal.size())
                            wp += v.boneWeight[i] * (pal[bi] * glm::vec4(v.position, 1.0f));
                    }
                    low = std::min(low, wp.y);
                }
                best = std::min(best, low);
                worst = std::max(worst, low);
                std::fprintf(stderr, "[gaitlow] %s q=%2d low=%+.4f m\n", which, q, (double)low);
            }
            std::fprintf(stderr,
                         "[gaitlow] %s cycle min=%+.4f m (max=%+.4f) -> constant offset %+.4f m\n",
                         which, (double)best, (double)worst, (double)-best);
        }
    }
    if (std::getenv("GLDX_FINGERS")) {
        for (int sd = 0; sd < gldx::kRigSides; ++sd) {
            std::fprintf(stderr, "[finger] %s:", sd == 0 ? "L" : "R");
            for (int f = 0; f < 5; ++f)
                for (int j = 0; j < 3; ++j)
                    std::fprintf(stderr, " %d", character.rig.finger[sd][f][j]);
            std::fprintf(stderr, "\n");
        }
    }
    if (std::getenv("GLDX_ARM")) {
        const auto& sk7 = model.skeleton();
        struct Probe { const char* name; gldx::AnimPoseSource* src; };
        Probe probes[] = {
            {"idle", character.idleSource.get()},
            {"walk", character.walkSource.get()},
            {"run", character.runSource.get()},
            {"crouchwalk", character.crouchWalkSource.get()},
            {"sit", character.sitSource.get()},
        };
        for (const Probe& pr : probes) {
            if (!pr.src) continue;
            const float dur = (pr.src->Duration() > 1e-4f) ? pr.src->Duration() : 1.0f;
            for (int q = 0; q < 4; ++q) {
                const float tt = dur * 0.25f * (float)q;
                std::vector<glm::mat4> pose, wm;
                pr.src->Sample(tt, pose);
                sk7.ComputeWorld(pose, model.Mat4(), wm);
                auto P = [&](int b) { return b < 0 ? glm::vec3(0) : glm::vec3(wm[(std::size_t)b][3]); };
                auto D = [&](int a, int c) {
                    const glm::vec3 d = P(c) - P(a);
                    return glm::length(d) > 1e-6f ? glm::normalize(d) : glm::vec3(0);
                };
                auto bx = [&](const glm::vec3& v) {
                    return glm::vec3(glm::dot(v, character.rig.forward),
                                     glm::dot(v, character.rig.up),
                                     glm::dot(v, character.rig.side));
                };
                for (int sd = 0; sd < gldx::kRigSides; ++sd) {
                    const glm::vec3 ua = bx(D(character.rig.armUp[sd], character.rig.armLow[sd]));
                    const glm::vec3 fa = bx(D(character.rig.armLow[sd], character.rig.hand[sd]));
                    std::fprintf(stderr,
                                 "[arm] %-10s t=%.2f %s upper fwd=%+.2f up=%+.2f side=%+.2f | "
                                 "fore fwd=%+.2f up=%+.2f side=%+.2f\n",
                                 pr.name, (double)tt, sd == 0 ? "L" : "R",
                                 (double)ua.x, (double)ua.y, (double)ua.z,
                                 (double)fa.x, (double)fa.y, (double)fa.z);
                }
            }
        }
        std::fprintf(stderr, "[arm] 目标(走/跑): upper up≈-0.90 side≈±0.15 fwd在±0.40内摆动; fore up≈-0.6..-0.9\n");
    }
    if (std::getenv("GLDX_SITLEG") && character.sitSource) {
        const auto& sk9 = model.skeleton();
        std::vector<glm::mat4> pose, wm;
        character.sitSource->Sample(0.0f, pose);
        sk9.ComputeWorld(pose, model.Mat4(), wm);
        auto P = [&](int b) { return b < 0 ? glm::vec3(0) : glm::vec3(wm[(std::size_t)b][3]); };
        for (int sd = 0; sd < gldx::kRigSides; ++sd) {
            const glm::vec3 hip = P(character.rig.legUp[sd]);
            const glm::vec3 knee = P(character.rig.legLow[sd]);
            const glm::vec3 ank = P(character.rig.foot[sd]);
            const glm::vec3 th = glm::normalize(knee - hip);
            const glm::vec3 sh = glm::normalize(ank - knee);
            std::fprintf(stderr,
                         "[sitleg] %s thigh fwd=%+.2f up=%+.2f side=%+.2f | shin fwd=%+.2f up=%+.2f side=%+.2f\n",
                         sd == 0 ? "L" : "R",
                         (double)glm::dot(th, character.rig.forward),
                         (double)glm::dot(th, character.rig.up),
                         (double)glm::dot(th, character.rig.side),
                         (double)glm::dot(sh, character.rig.forward),
                         (double)glm::dot(sh, character.rig.up),
                         (double)glm::dot(sh, character.rig.side));
        }
        std::fprintf(stderr, "[sitleg] 目标: 大腿 fwd~+0.7 up~-0.6 | 小腿 fwd~-0.6 up~-0.5 (W 坐)\n");
    }
    if (std::getenv("GLDX_SWIMAX") && character.swimSource) {
        const auto& sk6 = model.skeleton();
        std::vector<glm::mat4> pose, wm;
        character.swimSource->Sample(0.6f, pose);
        sk6.ComputeWorld(pose, model.Mat4(), wm);
        auto P = [&](int b) { return b < 0 ? glm::vec3(0) : glm::vec3(wm[(std::size_t)b][3]); };
        // Body axis: hips -> head. In a real prone swim this points horizontally forward.
        const glm::vec3 body = P(character.rig.head) - P(character.rig.hips);
        const glm::vec3 n = glm::length(body) > 1e-6f ? glm::normalize(body) : body;
        std::fprintf(stderr,
                     "[swimax] body(hips->head) up=%+.2f fwd=%+.2f side=%+.2f  "
                     "(prone 应为 up~0, |fwd|~1)\n",
                     (double)glm::dot(n, character.rig.up),
                     (double)glm::dot(n, character.rig.forward),
                     (double)glm::dot(n, character.rig.side));
        // Torso facing: chest normal. Prone => faces DOWN.
        const glm::vec3 chestFwd = glm::vec3(glm::vec3(wm[(std::size_t)character.rig.chest][2])[0], glm::vec3(wm[(std::size_t)character.rig.chest][2])[1], glm::vec3(wm[(std::size_t)character.rig.chest][2])[2]);
        std::fprintf(stderr, "[swimax] chest z-axis up=%+.2f fwd=%+.2f side=%+.2f\n",
                     (double)glm::dot(chestFwd, character.rig.up),
                     (double)glm::dot(chestFwd, character.rig.forward),
                     (double)glm::dot(chestFwd, character.rig.side));
    }
    if (std::getenv("GLDX_ELBOWDIR") && character.sourceFor(Action::Walk)) {
        const auto& sk5 = model.skeleton();
        std::vector<glm::mat4> pose, wm;
        // Sample whichever action the caller asked for - a probe pinned to the walk
        // source reports the same numbers for every `--action`, which reads as "all
        // actions are fine" when it only ever measured one.
        // NOTE: this runs before `--action` reaches the state machine, so it samples the
        // WALK source on the first frame regardless of the flag. That is fine for what
        // it exists for (does the elbow bend forward on BOTH sides - a property of the
        // pose sources, not of the action), but do not read it as "action X is checked".
        character.walkSource->Sample(0.0f, pose);
        sk5.ComputeWorld(pose, model.Mat4(), wm);
        for (int sd = 0; sd < gldx::kRigSides; ++sd) {
            const int bu = character.rig.armUp[sd], bl = character.rig.armLow[sd];
            const int hd = character.rig.hand[sd];
            if (bu < 0 || bl < 0 || hd < 0) continue;
            const glm::vec3 up = glm::normalize(glm::vec3(wm[(std::size_t)bl][3]) -
                                                glm::vec3(wm[(std::size_t)bu][3]));
            const glm::vec3 lo = glm::normalize(glm::vec3(wm[(std::size_t)hd][3]) -
                                                glm::vec3(wm[(std::size_t)bl][3]));
            // Elbow bends FORWARD: the forearm must gain forward component vs the upper arm.
            std::fprintf(stderr, "[elbowdir] %s upper.fwd=%+.2f fore.fwd=%+.2f  delta=%+.2f %s\n",
                         sd == 0 ? "L" : "R",
                         (double)glm::dot(up, character.rig.forward),
                         (double)glm::dot(lo, character.rig.forward),
                         (double)(glm::dot(lo, character.rig.forward) -
                                  glm::dot(up, character.rig.forward)),
                         (glm::dot(lo, character.rig.forward) > glm::dot(up, character.rig.forward))
                             ? "(forward, ok)" : "(BACKWARD - wrong)");
        }
    }
    if (std::getenv("GLDX_ARMPHASE") && character.walkSource) {
        const auto& sk = model.skeleton();
        for (int i = 0; i < 4; ++i) {
            const float ph = static_cast<float>(i) / 4.0f;
            std::vector<glm::mat4> pose, wm;
            character.walkSource->Sample(ph * character.walkSource->Duration(), pose);
            sk.ComputeWorld(pose, model.Mat4(), wm);
            auto P = [&](int b) { return b < 0 ? glm::vec3(0) : glm::vec3(wm[(std::size_t)b][3]); };
            const float handL = glm::dot(P(character.rig.hand[0]) - P(character.rig.chest),
                                         character.rig.forward);
            const float handR = glm::dot(P(character.rig.hand[1]) - P(character.rig.chest),
                                         character.rig.forward);
            const float footL = glm::dot(P(character.rig.foot[0]) - P(character.rig.hips),
                                         character.rig.forward);
            const float footR = glm::dot(P(character.rig.foot[1]) - P(character.rig.hips),
                                         character.rig.forward);
            std::fprintf(stderr,
                         "[armphase] ph=%.2f handL=%+.3f handR=%+.3f | footL=%+.3f footR=%+.3f\n",
                         (double)ph, (double)handL, (double)handR, (double)footL, (double)footR);
        }
    }
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
        std::fprintf(stderr, "[props] unitScale=%.4f rigHipsY=%.3f\n",
                     (double)model.unitScale(),
                     (double)glm::vec3(sk.RestWorld(character.rig.hips)[3]).y);
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
}  // namespace anim
