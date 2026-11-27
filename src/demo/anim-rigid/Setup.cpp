// Module first, then the headers that name its types.
import gldx;

#include "MmdNaming.h"
#include "Report.h"
#include "Setup.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cstddef>
#include <memory>
#include <utility>

namespace anim {

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
    // Sole plane relative to the ankle bone in BIND pose, i.e. how far the skin hangs
    // below the foot joint. `GroundContact` adds this to the contact point to know where
    // the floor actually is.
    //
    // It used to be "the lowest vertex of the whole model", which is the hem of the
    // skirt, not the sole - off by tens of centimetres, and the solve then parked the
    // character at whatever height made THAT point touch the floor. With the legs bound
    // to the real deform bones the skirt no longer follows the legs, so the error became
    // visible as a 0.5 m hover in `crouch`.
    bool firstVertex = true;
    {
        const int footL = character.rig.foot[gldx::kRigLeft];
        const glm::vec3 ankle =
            footL >= 0 ? glm::vec3(data.skeleton.RestWorld(footL)[3]) : glm::vec3(0.0f);
        for (const gldx::SkinnedMeshData& mesh : data.meshes) {
            for (const gldx::SkinnedVertex& v : mesh.vertices) {
                // Only vertices actually weighted to the feet: the skirt hangs below the
                // ankles too, and including it is the bug above.
                bool onFoot = false;
                for (int i = 0; i < 4; ++i) {
                    if (v.boneWeight[i] <= 0.0f) continue;
                    const int bi = static_cast<int>(v.boneIndex[i]);
                    if (bi == character.rig.foot[gldx::kRigLeft] ||
                        bi == character.rig.foot[gldx::kRigRight] ||
                        bi == character.rig.toe[gldx::kRigLeft] ||
                        bi == character.rig.toe[gldx::kRigRight]) {
                        onFoot = true;
                        break;
                    }
                }
                if (!onFoot) continue;
                // ABSOLUTE projection on `up`, not "below the ankle".
                //
                // `GroundContact::addContact` computes
                //     contactRig = bonePos - axis * (dot(bonePos, axis) - bindLowestOnUp)
                // i.e. it treats this number as "the lowest bind-space point along up"
                // and derives the sole offset from the joint's own height. Feeding it a
                // quantity measured RELATIVE to the ankle makes that subtraction eat the
                // ankle height twice: the solve then believes the soles are already on
                // the floor and never lowers the body - measured, `crouch` folded its
                // legs 22.9 cm off the ground while the pelvis stayed at standing height
                // (head 1.689 m vs idle 1.702 m).
                const float d = glm::dot(v.position, character.rig.up);
                character.bindLowestOnUp = firstVertex ? d
                                                        : std::min(character.bindLowestOnUp, d);
                firstVertex = false;
            }
        }
    }
    if (firstVertex) character.bindLowestOnUp = 0.0f;   // no foot-weighted verts

    // ---- keep one vertex per mesh, for the seated solve ----
    //
    // `GroundContact` only knows the four sole points, which is exactly wrong for a
    // sitting pose: the feet are off the floor and the thing touching it is the skirt.
    // Solving with the soles therefore reported "already on the floor" while the
    // character visibly floated, and the sit dropped by only 8 cm instead of the ~0.8 m
    // the authored seated height asks for.
    //
    // A decimated copy of the mesh (every Nth vertex) is enough to find the lowest
    // point to within a millimetre or two, costs one vector of ~10k entries instead of
    // 800k, and has to be taken BEFORE Upload moves `data` away.
    // How many vertices are weighted to the upper leg at all? If that number is ~0 the
    // leg bone animates a mesh nobody is attached to, which looks exactly like "the
    // pose does not reach the render".
    {
        const int lu = character.rig.legUp[gldx::kRigLeft];
        std::size_t weighted = 0, total = 0;
        for (const gldx::SkinnedMeshData& mesh : data.meshes) {
            for (const gldx::SkinnedVertex& v : mesh.vertices) {
                ++total;
                for (int i = 0; i < 4; ++i)
                    if (v.boneWeight[i] > 0.0f && static_cast<int>(v.boneIndex[i]) == lu) {
                        ++weighted;
                        break;
                    }
            }
        }
        std::fprintf(stderr, "[anim-rigid] weights: %zu of %zu vertices on legUp.L (%d)\n",
                     weighted, total, lu);
        // Which bones actually carry weight? The inverse of the check above.
        std::vector<std::size_t> perBone(data.skeleton.size(), 0);
        for (const gldx::SkinnedMeshData& mesh : data.meshes) {
            for (const gldx::SkinnedVertex& v : mesh.vertices) {
                for (int i = 0; i < 4; ++i) {
                    if (v.boneWeight[i] <= 0.0f) continue;
                    const std::size_t bi = v.boneIndex[i];
                    if (bi < perBone.size()) ++perBone[bi];
                }
            }
        }
        for (int b : {character.rig.legUp[0], character.rig.legLow[0], character.rig.foot[0]}) {
            if (b < 0) continue;
            std::fprintf(stderr, "[anim-rigid]   bone %d '%s' carries %zu verts\n", b,
                         data.skeleton.bones()[(std::size_t)b].name.c_str(), perBone[(std::size_t)b]);
        }
        // ---- 裙摆链骨到底带不带顶点？----
        //
        // 这一条必须先回答，否则"裙摆解算"可能只是在改一组不带任何几何的骨骼：
        // 解算写得再对，画面也不会动，而"腿穿过裙面"看起来就像解算没生效。
        {
            const auto& sk = character.rig.chains[static_cast<int>(gldx::RigChainKind::Skirt)];
            std::size_t carrier = 0;
            for (int i = 0; i < sk.count; ++i) {
                const int b = sk.bones[i];
                if (b < 0) continue;
                carrier += perBone[(std::size_t)b];
            }
            std::fprintf(stderr,
                         "[anim-rigid] skirt chain: %d bones, %zu weighted vertices total\n",
                         sk.count, carrier);
            for (int i = 0; i < sk.count; ++i) {
                const int b = sk.bones[i];
                if (b < 0) continue;
                std::fprintf(stderr, "[anim-rigid]   skirt[%2d] bone %d '%s' -> %zu verts\n", i, b,
                             data.skeleton.bones()[(std::size_t)b].name.c_str(),
                             perBone[(std::size_t)b]);
            }
        }
        std::vector<std::pair<std::size_t, std::size_t>> top;
        for (std::size_t i = 0; i < perBone.size(); ++i)
            if (perBone[i] > 0) top.emplace_back(perBone[i], i);
        std::sort(top.rbegin(), top.rend());
        std::fprintf(stderr, "[anim-rigid]   top loaded bones:\n");
        for (std::size_t i = 0; i < std::min<std::size_t>(top.size(), 12); ++i)
            std::fprintf(stderr, "[anim-rigid]     %6zu  %s\n", top[i].first,
                         data.skeleton.bones()[top[i].second].name.c_str());
    }

    character.probeVerts.clear();
    {
        std::size_t total = 0;
        for (const gldx::SkinnedMeshData& mesh : data.meshes) total += mesh.vertices.size();
        const std::size_t stride = std::max<std::size_t>(1, total / 20000);
        character.probeVerts.reserve(total / stride + 1);
        std::size_t k = 0;
        for (const gldx::SkinnedMeshData& mesh : data.meshes) {
            for (const gldx::SkinnedVertex& v : mesh.vertices) {
                if (k++ % stride == 0) character.probeVerts.push_back(v);
            }
        }
    }

    if (std::getenv("GLDX_MESHDOM")) {
        for (std::size_t mi = 0; mi < data.meshes.size(); ++mi) {
            const gldx::SkinnedMeshData& mesh = data.meshes[mi];
            std::vector<std::size_t> cnt(data.skeleton.size(), 0);
            for (const gldx::SkinnedVertex& v : mesh.vertices)
                for (int k = 0; k < 4; ++k)
                    if (v.boneWeight[k] > 0.5f) {
                        const std::size_t bi = v.boneIndex[k];
                        if (bi < cnt.size()) ++cnt[bi];
                    }
            std::size_t best = 0, bestN = 0;
            for (std::size_t b = 0; b < cnt.size(); ++b)
                if (cnt[b] > bestN) { bestN = cnt[b]; best = b; }
            if (bestN > 0)
                std::fprintf(stderr, "[meshdom] mesh %2zu mat=%d verts=%6zu -> bone %zu '%s' (%zu)\n",
                             mi, data.meshMaterial[mi], mesh.vertices.size(), best,
                             data.skeleton.bones()[best].name.c_str(), bestN);
        }
    }
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


    // ---- collect MMD twist bones that hang directly under the upper arm ----
    //
    // The naming family cannot express this: a twist bone is not a slot, it is extra
    // geometry bone the exporter inserted. Matching by the `捩` character and checking
    // that its PARENT is the upper arm keeps this honest - a twist bone under the
    // forearm already inherits the elbow and needs nothing.
    {
        const auto& bones = skeleton.bones();
        for (std::size_t i = 0; i < bones.size(); ++i) {
            if (bones[i].name.find("捩") == std::string::npos) continue;
            const int parent = bones[i].parent;
            for (int sd = 0; sd < gldx::kRigSides; ++sd) {
                if (parent >= 0 && parent == character.rig.armUp[sd])
                    character.twistBones.push_back(static_cast<int>(i));
            }
        }
        // Bind-pose offset of each twist bone in its forearm's frame.
        character.twistForearmOffset.clear();
        for (int tb : character.twistBones) {
            int sd = 0;
            for (; sd < gldx::kRigSides; ++sd)
                if (character.rig.armUp[sd] >= 0 &&
                    skeleton.bones()[(std::size_t)tb].parent == character.rig.armUp[sd])
                    break;
            const int low = (sd < gldx::kRigSides) ? character.rig.armLow[sd] : -1;
            if (low < 0) { character.twistForearmOffset.emplace_back(0.0f); continue; }
            const glm::mat4 invForearm = glm::inverse(skeleton.RestWorld(low));
            const glm::vec3 tbRest(skeleton.RestWorld(tb)[3]);
            character.twistForearmOffset.emplace_back(glm::vec3(invForearm * glm::vec4(tbRest, 1.0f)));
        }

        if (!character.twistBones.empty()) {
            std::string names;
            for (int b : character.twistBones)
                names += (names.empty() ? "" : ", ") + skeleton.bones()[(std::size_t)b].name;
            std::fprintf(stderr, "[anim-rigid] twist bones under the upper arm: %s\n",
                         names.c_str());
        }
    }

    // ---- common ancestor of hips and spine ----
    if (character.rig.hips >= 0 && character.rig.spine >= 0) {
        auto lineage = [&](int b) {
            std::vector<int> out;
            while (b >= 0) { out.push_back(b); b = skeleton.bones()[(std::size_t)b].parent; }
            return out;
        };
        const std::vector<int> a = lineage(character.rig.hips);
        const std::vector<int> b = lineage(character.rig.spine);
        for (int x : a)
            if (std::find(b.begin(), b.end(), x) != b.end()) { character.bodyRoot = x; break; }
        if (character.bodyRoot >= 0)
            std::fprintf(stderr, "[anim-rigid] body root (hips+spine LCA) = %d '%s'\n",
                         character.bodyRoot,
                         skeleton.bones()[(std::size_t)character.bodyRoot].name.c_str());
    }

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
            // The ratio lands the pelvis at a plausible HEIGHT, but "plausible" is not
            // "touching": measured with `GLDX_LOW`, the lowest point of the seated
            // pose sits 3.86 cm above the floor - the character hovers, and a hovering
            // sit reads as a posing error rather than a sitting one.
            //
            // Seat-on-floor is a different constraint from pelvis-at-ratio-height, so
            // solve it directly: drop the pelvis by whatever the sole probe reports,
            // which is exactly the number the report prints. Deriving it from the
            // measurement (rather than subtracting a hand-tuned 0.0386) keeps it
            // correct when the leg ratio or the pose angles change.
            character.seatedHip = model.legLengthMetres() * Character::kSeatedHipRatio;
            character.seatedHipDerived = true;
            const glm::vec4 hips =
                restPalette[(std::size_t)character.rig.hips] *
                glm::vec4(-glm::vec3(skeleton.bone(character.rig.hips)->inverseBind[3]), 1.0f);
            // ---- seated height: solve it, do not scale it ----
            //
            // The obvious formulation - `sitRise = seatedHip - hips.y` - hides a unit
            // assumption that only holds for the rig it was written on. `seatedHip` is
            // authored in metres; the pelvis position comes out of ComputeSkinning in
            // whatever space the palette uses. On the PMX export those happen to be
            // within 1 % (unitScale 0.1, hips at 12.095 -> 1.21 m), so the mistake was
            // invisible; the FBX export of the SAME character has unitScale 0.01 and
            // hips at 96.8, and the same line put the soles 0.977 m in the air
            // (`sit <- FLOATS`).
            //
            // Rather than guess which space the palette is in, measure the answer: pose
            // the sit source at t=0, skin it, and ask GroundContact where the body
            // actually ended up. Then shift by the difference. That is one solve at
            // load time - the frame loop keeps using the closed-form `sitRise` - and it
            // cannot disagree with the rig, because it reads the rig.
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
    // ---- which numbers this rig gets ----
    //
    // The engine's defaults ARE the MMD/PMX template: that rig is the one with clean
    // weights, so its numbers are what "a correct rig" looks like, and the engine is
    // the right place for them. Every other asset starts from that template and
    // overrides only what its skeleton cannot take - see CoarseWalkTuning & co. in
    // Tunings.h, which INHERIT the engine struct and change a handful of fields rather
    // than copying a whole set. A full copy would make "what did we change and why"
    // unreadable, which for tuning numbers is the only thing worth reading.
    // EVERY rig gets the engine's template. The MMD asset is the reference
    // (clean weights, symmetric bind) and its values ARE the engine defaults, so
    // there is nothing to override for it; the other rig takes the same numbers.
    //
    // It used to get `CoarseSkinnedTunings` (smaller amplitudes + a bigger armDown
    // derived from its own asymmetric bind). That is abandoned deliberately: the
    // task is to design ONE good template on the clean rig, not to keep two sets of
    // numbers in sync. A rig that cannot take the reference amplitudes is a rig that
    // needs its bind fixed, and papering over it with a second tuning hides that.
    const animrigid::PoseTunings tunings = animrigid::TuningsFor(
        animrigid::NamingChannelForTunings::Rigid);

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
    // Hoisted out of the block below on purpose: `crouchSource` must be built from the
    // SAME curve, or "crouch is crouch-walk held at one phase" is just a claim.
    //
    // It used to be passed `character.tune` (the STANDING walk), while the crouch-walk
    // source got the narrowed swing below. The two then differed by 0.236 in the report
    // line that exists to prove they agree - i.e. pressing/selecting `crouch` produced
    // a visibly different stance from `crouchwalk` at the same phase, and the check
    // that was supposed to catch it had been reporting the failure for a while.
    gldx::WalkTuning crouchTune = character.tune;
    {
        crouchTune.crouch = 1.0f;
        // `strideScale()` divides by this, so it must be the STANDING swing - set it
        // before the crouch multipliers below shrink `hipSwing`.
        crouchTune.baseHipSwing = character.tune.hipSwing;
        // A crouching walk does NOT take the standing stride amplitudes.
        //
        // Inheriting `character.tune` outright meant this gait swung at 26 deg hip /
        // 65 deg knee while a real crouch-walk is about 14 / 35 - i.e. it was at
        // 175-184 % of human, which reads as "sneaking on tiptoe at full walking
        // speed" rather than as crouching. `crouch = 1` only folds the legs; it does
        // not shrink the swing, so the swing has to be shrunk here.
        crouchTune.hipSwing      *= 0.54f;   // 26 -> 14 deg
        crouchTune.kneeSwingFlex *= 0.55f;   // 65 -> 35 deg
        crouchTune.kneeStanceFlex *= 0.58f;  // 17 -> 10 deg
        crouchTune.armSwing      *= 0.57f;   // 32 -> 18 deg
        // `strideScale()` divides `hipSwing` by `baseHipSwing`, but that ratio is not
        // the whole story: with the legs folded (crouch = 1) a given hip angle moves
        // the foot through a much smaller horizontal arc, because the knee is bent
        // roughly square. Measured on this rig: standing foot span 0.751 m, crouching
        // foot span 0.209 m for 0.54x the hip swing - i.e. the foot covers 0.28 of the
        // standing stride, not 0.54.
        //
        // That extra factor is a GEOMETRY constant of the crouch pose, not of the
        // asset, but measuring it once here beats deriving it: the ground scroll then
        // matches what the planted foot actually does.
        crouchTune.baseHipSwing = character.tune.hipSwing * 2.20f;
        character.crouchWalkSource = std::make_unique<gldx::ProceduralWalk>(
            skeleton, character.rig, model.restWorld(), crouchTune,
            character.cycleSeconds(1.0f));
    }
    // Crouch is crouch-WALK's own curve held at one phase (see ProceduralCrouch.h):
    // the two actions cannot drift apart because there is only one curve. `report`
    // prints the byte-level delta between them so "same pose" stays a measurement.
    character.crouchSource = std::make_unique<gldx::ProceduralCrouch>(
        skeleton, character.rig, model.restWorld(), tunings.crouch, crouchTune);
    character.attackSource = std::make_unique<gldx::ProceduralAttack>(
        skeleton, character.rig, model.restWorld(), tunings.attack);
    character.jumpSource = std::make_unique<gldx::ProceduralJump>(
        skeleton, character.rig, model.restWorld(), tunings.jump);
    character.sitSource = std::make_unique<gldx::ProceduralSit>(
        skeleton, character.rig, model.restWorld(), tunings.sit);
    // ---- measure the seated drop instead of computing it ----
    //
    // Now that the source exists, ask it: pose the sit at t=0, skin it the way the
    // frame loop does, and read where the lowest contact point landed with
    // `sitRise = 0`. Whatever that number is, subtracting it puts the seat on the
    // floor - in whatever units this rig happens to use, on any asset.
    //
    // This runs ONCE at load (the frame loop keeps using the closed-form sitRise), so
    // it costs one skin pass and removes a whole class of "works on the rig it was
    // written on" bugs.
    {
        std::vector<glm::mat4> sitPose;
        character.sitSource->Sample(0.0f, sitPose);
        std::vector<glm::mat4> sitPalette;
        skeleton.ComputeSkinning(sitPose, model.Mat4(), sitPalette);

        // Skin the sampled vertices and take the true minimum height. This is the only
        // measurement that answers "how far off the floor is this pose" for a pose whose
        // support is the skirt rather than the soles.
        float lowest = 1.0e9f;
        float highest = -1.0e9f;
        for (const gldx::SkinnedVertex& v : character.probeVerts) {
            glm::vec4 p(0.0f);
            float wsum = 0.0f;
            for (int i = 0; i < 4; ++i) {
                const float w = v.boneWeight[i];
                if (w <= 0.0f) continue;
                const std::size_t bi = v.boneIndex[i];
                if (bi >= sitPalette.size()) continue;
                p += w * (sitPalette[bi] * glm::vec4(v.position, 1.0f));
                wsum += w;
            }
            if (!(wsum > 0.0f)) continue;   // unweighted vertices would read as y = 0
            lowest = std::min(lowest, p.y);
            highest = std::max(highest, p.y);
        }
        std::fprintf(stderr, "[anim-rigid] sit probe span y=[%.4f, %.4f]\n",
                     (double)lowest, (double)highest);
        std::fprintf(stderr,
                     "[anim-rigid] sit slots: legUp.L=%d legLow.L=%d foot.L=%d  "
                     "legUp.R=%d legLow.R=%d foot.R=%d\n",
                     character.rig.legUp[0], character.rig.legLow[0], character.rig.foot[0],
                     character.rig.legUp[1], character.rig.legLow[1], character.rig.foot[1]);

        // `sitRise` is added on top of `restLift` in the frame loop, and the palette above
        // already carries `restLift`'s equivalent (we did NOT apply the solve here), so
        // the correction is simply "-lowest", measured in the same units it is applied in.
        // `lowest` is in the palette's units, which are the units `sitRise` is applied
        // in - so the correction is exactly `-lowest`. No scale conversion here: the
        // frame loop adds sitRise to the same palette, so both sides are the same space
        // by construction. (An earlier version mixed a metre figure into this line and
        // produced a 2.3 mm correction for a character floating 0.85 m up.)
        // Do NOT add a correction: the seated pose is now genuinely where it should be.
        //
        // With the legs bound to their real (D-suffix) deform bones the sit pose finally
        // collapses - the skin follows the bones - so `lowest` is already near 0 and the
        // old "-lowest" compensation (which existed to rescue a pose that never moved)
        // just pushes the character underground. Measured both ways: without it
        // `lowestY = 0.0`, with it `-0.668` (`sit <- SINKS`).
        // ---- 用"承重部位"的最低点定坐高，且**不要硬编码** ----
        //
        // 这里原本是 `sitRise = 0.10f`（一个旧姿势下试出来的常数），而姿势后来整个
        // 换过（髋角符号、外展、扭转都改了），那个数就变成"把角色架在半空 10 cm"。
        // 画面里坐姿一直是悬空的，HUD 的 `shift +100.0 mm` 正是它。
        //
        // 也不能用"-lowest"：`lowest` 是**全身**最低点，而这具资产上最低的是一条垂
        // 下来的缎带（实测骨骼 `右リボン大２`）。把缎带放到地面，臀部就停在 20 cm
        // 高处——"人坐在缎带上"。坐姿的承重件是臀、大腿、小腿、脚，所以只量这些。
        float supportLow = 1.0e9f;
        {
            std::vector<int> support;
            if (character.rig.hips >= 0) support.push_back(character.rig.hips);
            for (int b : character.rig.legUp) support.push_back(b);
            for (int b : character.rig.legLow) support.push_back(b);
            for (int b : character.rig.foot) support.push_back(b);
            for (const gldx::SkinnedVertex& v : character.probeVerts) {
                bool ok = false;
                for (int i = 0; i < 4; ++i) {
                    if (v.boneWeight[i] <= 0.0f) continue;
                    const int bi = static_cast<int>(v.boneIndex[i]);
                    if (std::find(support.begin(), support.end(), bi) != support.end()) {
                        ok = true;
                        break;
                    }
                }
                if (!ok) continue;
                glm::vec4 q(0.0f);
                for (int i = 0; i < 4; ++i) {
                    if (v.boneWeight[i] <= 0.0f) continue;
                    const std::size_t bi = v.boneIndex[i];
                    if (bi < sitPalette.size())
                        q += v.boneWeight[i] * (sitPalette[bi] * glm::vec4(v.position, 1.0f));
                }
                supportLow = std::min(supportLow, q.y);
            }
        }
        // `sitRise` is left at 0: the frame loop grounds a seated pose from the real
        // skinned body every frame (see the `seated` branch), which cannot disagree with
        // the picture. Keeping a load-time constant here as well meant two corrections
        // fought each other, and whichever ran last won - "changed the number, nothing
        // moved on screen".
        character.sitRise = 0.0f;
        std::fprintf(stderr, "[anim-rigid] sit support low %.4f -> sitRise %.4f\n",
                     (double)supportLow, (double)character.sitRise);
        std::fprintf(stderr,
                     "[anim-rigid] seated drop measured: lowest %.4f -> sitRise %.4f "
                     "(rig units); seatedHip %.3f m, restLift %.4f, %zu probe verts\n",
                     (double)lowest, (double)character.sitRise,
                     (double)character.seatedHip, (double)character.restLift,
                     character.probeVerts.size());
        // ---- where does the pelvis actually sit, and where should it? ----
        //
        // Folding the legs is not the same as lowering the body: the pose sources only
        // write joint angles, so the pelvis stays at standing height and the character
        // hovers with its legs splayed to the sides. `seatedHip` is the authored absolute
        // pelvis height; the drop is measured from this rig rather than assumed to be in
    }
    character.swimSource = std::make_unique<gldx::ProceduralSwim>(
        skeleton, character.rig, model.restWorld(), tunings.swim);
    // Face down. The sign is the difference between freestyle and backstroke, and it
    // now lives in the pose source so it shares a frame with the arm targets.
    character.swimSource->SetProneAngle(1.55f);
    // Rotate the common ancestor of legs and spine, NOT `hips` alone - on MMD those two
    // are siblings, so rotating hips leaves the torso upright (body axis up=+0.97).
    character.swimSource->SetProneBone(character.bodyRoot);
    // Chain parameters, one set per kind. Numbers are per-asset: the skirt here is a
    // 12-panel MMD hem, which is much softer than a tailor-made cape.
    {
        using gldx::RigChainKind;
        auto& t = character.chainTune;
        // {stiffness, damping, gravity}
        //
        // `damping` is the per-step velocity retention, so 0.88 means the spring is
        // almost dead after ~8 frames: measured, the hem's shape across four run phases
        // was nearly identical, i.e. the cloth read as a rigid cone that only leaned
        // slightly. Real fabric keeps swinging for most of a stride, so damping comes
        // down and stiffness with it (a stiff spring with low damping rings; a soft one
        // with low damping swings).
        // `strandLength = 3`: the skirt is 8 panels x 3 joints flattened into one chain,
        // and the response must depend on depth ALONG a panel, not on which panel it is.
        // With the old flattened-index gain, panel 1 moved at 0.45 and panel 8 at 1.0 -
        // one side of the hem puffed out while the other stayed skin-tight, and it never
        // evened out because the two sides were being driven at different strengths.
        // gravity 从 0.30 改成 0：那一项是**常数向后仰**，不是"垂下来"。整圈下摆
        // 永久停在身后 17°，走路时也照样停在那儿——"裙子一直往后、而且不动"就是它。
        // 静止时不加偏置，rest 姿态本来就是自然下垂的。
        gldx::ChainTuning skirtTune{0.10f, 0.960f, 0.0f, /*tuck*/-0.16f};
        skirtTune.strandLength = 3;
        // 随步频摆起来：每迈一步身体的上下起伏与前后摆给出非零加速度，布面按
        // -acc 滞后。没有这一项时，原地走的 demo 驱动量恒为 0。
        skirtTune.inertia  = 0.20f;
        skirtTune.inertiaZ = 0.045f;
        // 腿扫过时把下摆拖着走（m/s -> rad）。这是"走路时裙子在动"的主要来源。
        skirtTune.probeDrag = 1.10f;
        // 步态驱动（m -> rad）。膝盖前后摆约 ±0.16 m，乘 2.6 得到约 ±0.4 rad 的摆动。
        skirtTune.gaitDrive = 2.60f;
        t[static_cast<int>(RigChainKind::Skirt)] = skirtTune;
        t[static_cast<int>(RigChainKind::Hair)]  = gldx::ChainTuning{0.13f, 0.970f, 0.18f};
        t[static_cast<int>(RigChainKind::Tail)]  = gldx::ChainTuning{0.12f, 0.965f, 0.12f};
        t[static_cast<int>(RigChainKind::Ear)]   = gldx::ChainTuning{0.45f, 0.80f, -0.10f};

        // 静止微动：头发最明显（它最轻、最长），尾巴次之，裙摆再小一档（布料比头发重，
        // 不动才是对的），耳朵几乎不抖。放在这个初始化里而不是每个动作源里，是因为
        // "角色是活的"与动作无关——坐下、待机、站着说话时都要有。
        t[static_cast<int>(RigChainKind::Hair)].breathe   = 0.030f;
        t[static_cast<int>(RigChainKind::Hair)].breatheHz = 0.42f;
        t[static_cast<int>(RigChainKind::Tail)].breathe   = 0.024f;
        t[static_cast<int>(RigChainKind::Tail)].breatheHz = 0.35f;
        t[static_cast<int>(RigChainKind::Ear)].breathe    = 0.010f;
        t[static_cast<int>(RigChainKind::Ear)].breatheHz  = 0.55f;
        t[static_cast<int>(RigChainKind::Skirt)].breathe  = 0.006f;
        t[static_cast<int>(RigChainKind::Skirt)].breatheHz = 0.33f;
        // 取证开关：把微动整体关掉，用来分清"末端的摆动来自微动"还是"来自待机动作本身"。
        if (const char* off = std::getenv("GLDX_NOBREATHE")) {
            if (off[0] == '1')
                for (int k = 0; k < gldx::RigChainKindCount; ++k) t[k].breathe = 0.0f;
        }
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

    if (reportGait) ReportGait(character, traceAction);
}
}  // namespace anim
