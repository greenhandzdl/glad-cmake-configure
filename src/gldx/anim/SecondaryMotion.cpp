module;

#include "gldx/gmf.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

module gldx;

namespace gldx {

namespace {

// How much of the root's motion a joint at this depth should feel. The tip of a chain
// moves MORE than its root for the same body motion (that is what makes a skirt flare
// when you turn), so the drive grows along the chain.
/**
 * How strongly a joint responds, by how DEEP it is along its own strand.
 *
 * This used to be `index / (count - 1)` over the FLATTENED chain. For the skirt that
 * chain is 8 panels x 3 joints concatenated, so the flattened index mixes two different
 * things: the panel number and the depth. Measured effect: panel 1 got gain 0.45-0.52
 * and panel 8 got 0.94-1.00 - the hem was visibly puffy on one side of the body and
 * skin-tight on the other, and it never converged because the two sides were being
 * driven by different strengths.
 *
 * `strandLength` is how many joints one strand has (3 for the skirt, 1 when the chain
 * is a single rope like the tail), so the gain now depends only on depth.
 */
inline float DepthGain(int index, int count, int strandLength) {
    if (count <= 1) return 1.0f;
    const int strand = (strandLength > 0) ? strandLength : count;
    if (strand <= 1) return 1.0f;
    const float u = static_cast<float>(index % strand) / static_cast<float>(strand - 1);
    return 0.45f + 0.55f * u;
}

// A spring step that cannot explode. The semi-implicit form (update omega, then angle)
// is stable for the stiffness ranges we use; the clamp is there because `dt` can spike
// on the first frames after load and an unbounded spring becomes a NaN fountain that
// takes the whole palette with it.
inline void StepSpring(float& angle, float& omega, float target, float stiffness,
                       float damping, float dt) {
    const float k = std::clamp(stiffness, 0.0f, 1.0f) * 60.0f;
    const float c = std::clamp(damping, 0.0f, 1.0f);
    omega += (target - angle) * k * dt;
    omega *= std::pow(c, dt * 60.0f);
    angle += omega * dt;
    // 0.6 rad (~34 deg) is well inside a skirt panel's real range and far short of
    // self-intersection; beyond that the lag reads as a glitch, not as fabric.
    angle = std::clamp(angle, -0.6f, 0.6f);
    if (std::abs(omega) < 1e-5f) omega = 0.0f;
}

}  // namespace

void ChainSolver::Reset(const RigProfile::RigChain& chain) {
    const std::size_t n = static_cast<std::size_t>(std::max(chain.count, 0));
    angle_.assign(n, glm::vec3(0.0f));
    omega_.assign(n, glm::vec3(0.0f));
    prevRootPos_.assign(n, glm::vec3(0.0f));
    primed_ = false;
}

void ChainSolver::Solve(const RigProfile::RigChain& chain, std::vector<glm::mat4>& local,
                        const std::vector<glm::mat4>& world,
                        const std::vector<glm::mat4>& restWorld,
                        const ChainTuning& tuning, float dt) {
    // The whole point of making chains optional: an asset without a tail, a skirt or
    // hair simply has count == 0 here and pays nothing.
    if (!chain.present()) return;
    if (angle_.size() != static_cast<std::size_t>(chain.count)) Reset(chain);
    // `dt == 0` means "do not advance", NOT "do nothing".
    //
    // The frame loop rebuilds the pose from the source every frame, then re-applies the
    // chains. Returning early here left the already-simulated angles un-written, so the
    // pose fell back to the source's rest shape and the cloth/hair/tail snapped to
    // zero deflection - measured as `maxDevFromRest 0.30 -> 0.000` between two
    // consecutive frames, and from outside as "identical to --off secondary" (0 differing
    // pixels). The settle pass was correct; the next frame undid it.
    //
    // A frozen clock is exactly this situation, so the apply path has to keep running
    // while the integrate path stops.
    const bool advance = dt > 0.0f;
    // 微动时钟跟着真实时间走（不是跟着姿势时间）——它就是"这具身体还活着"的那条时间轴。
    if (advance) clock_ += dt;

    const int count = chain.count;

    // Root motion: how far the chain's base moved this frame, and how much it rotated.
    // Derived from the world matrices rather than from the pose source, because the
    // source does not know about ground lift / vertical shift - and a skirt that
    // ignores the character's vertical bob is the classic "detached cloth" look.
    const int rootBone = chain[0];
    if (rootBone < 0 || rootBone >= static_cast<int>(world.size())) return;

    glm::vec3 rootPos(world[static_cast<std::size_t>(rootBone)][3]);
    glm::vec3 rootUp = glm::normalize(glm::vec3(world[static_cast<std::size_t>(rootBone)][1]));

    if (!primed_) {
        for (int i = 0; i < count; ++i) prevRootPos_[static_cast<std::size_t>(i)] = rootPos;
        prevRootUp_ = rootUp;
        primed_ = true;
        return;   // no history -> nothing to lag behind yet
    }

    // Horizontal acceleration is the drive: a steady walk should leave the skirt
    // hanging, a sudden turn or a landing should swing it. Using raw velocity would
    // make it lean permanently backwards while walking, which looks like wind.
    const glm::vec3 delta = rootPos - prevRootPos_[0];
    const glm::vec3 up = rootUp;
    const glm::vec3 side = glm::normalize(glm::cross(up, glm::vec3(0.0f, 0.0f, 1.0f) + up * 1e-3f));
    const glm::vec3 fwd = glm::normalize(glm::cross(side, up));

    const float leanFwd = glm::dot(delta, fwd);
    const float leanSide = glm::dot(delta, side);
    // Vertical motion drives the "flare" axis: landing pushes the hem out, jumping
    // pulls it in. This is the term that makes a jump read as a jump rather than the
    // character being teleported.
    const float liftAmount = glm::dot(delta, up);

    for (int i = 0; i < count; ++i) {
        const std::size_t j = static_cast<std::size_t>(i);
        const int bone = chain[i];
        if (bone < 0 || bone >= static_cast<int>(local.size())) continue;

        const float gain = DepthGain(i, count, tuning.strandLength);

        // Targets are expressed in the joint's own frame: `PoseRotation` takes a
        // world-space axis and folds it into the rest-local frame, so passing the
        // rig's axes here keeps this solver independent of the rig's handedness.
        glm::vec3 target(0.0f);
        target.x = -leanFwd * gain * 8.0f;             // pitch: lean back when moving fwd
        target.y = -leanSide * gain * 8.0f;            // yaw: swing to the outside of a turn
        target.z = -liftAmount * gain * 6.0f;          // roll: flare on vertical motion
        target += glm::vec3(tuning.gravity * gain, 0.0f, 0.0f);   // hang under gravity

        // ---- 呼吸微动 ----
        //
        // 上面每一项都来自"根在动"或"腿在推"。角色站着不动时它们全是 0，弹簧收敛
        // 到常数，链就彻底冻结了——头发/裙摆读作硬塑料。这里叠一个很慢很小的周期量，
        // 相位按束错开（11 束头发、8 片裙摆不能同步摆），末端幅度更大。
        if (tuning.breathe > 0.0f) {
            const int strandLen = std::max(tuning.strandLength, 1);
            const float ph = clock_ * 6.2831853f * tuning.breatheHz;
            const float strandPhase = 0.9f * static_cast<float>(i % strandLen) +
                                      2.1f * static_cast<float>(i / strandLen);
            target.x += tuning.breathe * gain * std::sin(ph + strandPhase);
            target.z += tuning.breathe * gain * 0.75f * std::cos(ph * 0.77f + strandPhase * 1.7f);
        }
        // Static tuck, SIGNED PER PANEL.
        //
        // `target` is expressed about WORLD axes (see PoseRotation above), so a single
        // signed `tuck` rotates every panel the same way in world space - the whole skirt
        // leans to one side instead of closing toward the midline. Which way a panel has
        // to roll to move inward depends on which side of the body it hangs on, so the
        // sign comes from the panel's own lateral offset from the chain root.
        if (tuning.tuck != 0.0f) {
            const glm::vec3 rootPos(world[chain[0]][3]);
            const glm::vec3 sideAxis = glm::normalize(glm::vec3(world[chain[0]][0]));
            const float lateral = glm::dot(glm::vec3(world[j][3]) - rootPos, sideAxis);
            const float tuckSign = (lateral >= 0.0f) ? 1.0f : -1.0f;
            // 幅度随"离中线多远"变化。
            //
            // 这具资产的裙片挂点本身就是歪的：量出来的根部半径一侧 +0.198 m、另一侧
            // 只有 -0.071 m。等幅度内收救不了这种偏置——宽的一侧还是垂得更远。按
            // 侧向距离加权之后，宽侧被收得更多，裙摆的重心才回到中线附近。
            const float tuckScale =
                std::clamp(std::abs(lateral) / 0.16f, 0.25f, 1.75f);
            target.z += tuning.tuck * gain * tuckSign * tuckScale;
        }

        // ---- cloth-vs-leg push ----
        //
        // Each joint is pushed AWAY from the nearest leg point, by how much the leg is
        // inside it. This is what makes the hem hug the leg when it is close and swing
        // out when the leg pushes through - without it the cloth only follows the root's
        // acceleration and a walking leg simply intersects it.
        //
        // Applied on the two axes the joint can actually move in (x = pitch, z = roll);
        // the yaw axis is left to the turn response. The sign comes from the geometry so
        // no per-asset handedness is assumed.
        if (!legPoints_.empty() && legRadius_ > 0.0f) {
            const glm::vec3 jointPos(world[j][3]);
            glm::vec3 push(0.0f);
            for (std::size_t pi = 0; pi < legPoints_.size(); ++pi) {
                const glm::vec3& lp = legPoints_[pi];
                const float radius = pi < legRadii_.size() ? legRadii_[pi] : legRadius_;
                if (radius <= 0.0f) continue;
                const glm::vec3 away = jointPos - lp;
                const float dist = glm::length(away);
                if (dist < 1e-5f || dist >= radius) continue;
                // How deep the leg is inside this joint, as a 0..1 fraction of the radius.
                const float depth = (radius - dist) / radius;
                push += glm::normalize(away) * depth;
            }
            if (glm::length(push) > 1e-5f) {
                push = glm::normalize(push);
                const float mag = std::min(glm::length(push) * 2.0f, 1.0f);
                target.x += push.x * mag * 0.9f;
                target.z += push.z * mag * 0.9f;
            }
        }

        if (advance) {
            StepSpring(angle_[j].x, omega_[j].x, target.x, tuning.stiffness, tuning.damping, dt);
            StepSpring(angle_[j].y, omega_[j].y, target.y, tuning.stiffness, tuning.damping, dt);
            StepSpring(angle_[j].z, omega_[j].z, target.z, tuning.stiffness, tuning.damping, dt);
        }

        // Apply as three small rotations in the joint's local frame. Composing small
        // angles is order-dependent in principle; at the 0.6 rad clamp the error is
        // below what a 12-panel skirt resolves, and the alternative (full quaternion
        // integration) would buy nothing visible here.
        glm::mat4 rot(1.0f);
        rot = glm::rotate(rot, angle_[j].x, glm::vec3(1.0f, 0.0f, 0.0f));
        rot = glm::rotate(rot, angle_[j].y, glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, angle_[j].z, glm::vec3(0.0f, 0.0f, 1.0f));
        local[static_cast<std::size_t>(bone)] = local[static_cast<std::size_t>(bone)] * rot;
    }

    prevRootPos_[0] = rootPos;
    prevRootUp_ = rootUp;
}

}  // namespace gldx
