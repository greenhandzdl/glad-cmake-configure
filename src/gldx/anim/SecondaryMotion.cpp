module;

#include <cstdlib>

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
    // 差分历史必须一起清。
    //
    // 只清角度/角速度不够：`prevRootVel_` ���**上一帧的速度**，重置后第一次推进时
    // 它仍是旧值，于是 `acc = (vel - prevRootVel_)/dt` 凭空产生一个加速度（被限幅到
    // 40 m/s² 之后仍有 40）。结算循环跑 600 步，这个虚假加速度会一直推动布面，而
    // 旧速度取决于"冻结前跑了多少帧"——症状正是同一个快照两次跑出不同结果。
    prevRootVel_ = glm::vec3(0.0f);
    prevProbePos_.clear();
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
    // 不再自己累加：见 SetClock() 的注释。调用方没设过时保持 0，呼吸就是常数相位。

    const int count = chain.count;
    const int strandLenDbg = tuning.strandLength;

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

    // 根的**速度**与**加速度**。
    //
    // 上面三个量用的还是每帧位移 `delta`：那是"每帧多少米"，本身带上了帧率
    // （120 Hz 与 60 Hz 差一倍），只是历史上按 120 Hz 调过来才显得能用。这里
    // 一并换成每秒量，惯性和帧率就脱钩了。
    // `advance` 决定"这一帧是不是真的过去了"。
    //
    // Solve 每帧会被调用很多次：冻结的前 kSettleSteps 步是一个循环里的几百次
    // `advance=false` 的迭代。若在那些迭代里也更新上一帧位置，速度就永远是
    // "当前帧减当前帧"= 0——探针拖拽、惯性这些**差分**驱动会整体失效，而且失效得
    // 很隐蔽：画面只是"不动"，看起来像没接上。
    const float idt = 1.0f / std::max(dt, 1.0e-4f);
    const glm::vec3 vel = advance ? delta * idt : glm::vec3(0.0f);
    glm::vec3 acc = advance ? (vel - prevRootVel_) * idt : glm::vec3(0.0f);
    // 首帧/断电后重置时加速度会炸，限幅到 40 m/s²（约 4 g 的粗活）。
    if (glm::length(acc) > 40.0f) acc = glm::normalize(acc) * 40.0f;
    if (advance) prevRootVel_ = vel;
    const float accFwd = glm::dot(acc, fwd);
    const float accUp  = glm::dot(acc, up);

    // ---- 探针速度：腿扫过时把布"拖"着走 ----
    //
    // 探针只被用来"推开"布，所以腿一旦离开半径，布立刻回位，中间那段什么都没有。
    // 加上速度项之后，腿扫过的整个过程布都被拖着，走过去以后再由弹簧摆回来——
    // 一步一次可见摆动，方向跟着腿走。
    std::vector<glm::vec3> probeVel(legPoints_.size(), glm::vec3(0.0f));
    if (tuning.probeDrag != 0.0f && advance) {
        if (prevProbePos_.size() != legPoints_.size()) {
            prevProbePos_.assign(legPoints_.size(), glm::vec3(0.0f));
            for (std::size_t pi = 0; pi < legPoints_.size(); ++pi)
                prevProbePos_[pi] = legPoints_[pi];
        }
        if (std::getenv("GLDX_PROBEDBG")) {
            static int n = 0;
            if (n++ % 20 == 0)
                std::fprintf(stderr, "[probe] n=%zu prev0=(%.3f,%.3f,%.3f) cur0=(%.3f,%.3f,%.3f)\n",
                             legPoints_.size(), (double)prevProbePos_[0].x,
                             (double)prevProbePos_[0].y, (double)prevProbePos_[0].z,
                             (double)legPoints_[0].x, (double)legPoints_[0].y,
                             (double)legPoints_[0].z);
        }
        for (std::size_t pi = 0; pi < legPoints_.size(); ++pi) {
            glm::vec3 v = (legPoints_[pi] - prevProbePos_[pi]) * idt;
            if (glm::length(v) > 30.0f) v = glm::normalize(v) * 30.0f;   // 传送/切动作时别炸
            probeVel[pi] = v;
            prevProbePos_[pi] = legPoints_[pi];
        }
    }

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
        target += glm::vec3(tuning.gravity * gain, 0.0f, 0.0f);   // 常数偏置（见头文件）

        // ---- 步态驱动：前片跟前面的腿，后片跟后面的腿 ----
        //
        // **直接叠加到关节角度**，不走弹簧。理由：`StepSpring` 的固有频率上限是
        // `sqrt(1.0*60)=7.7 rad/s`（约 1.2 Hz），而步频约 1 Hz —— 弹簧跟这种周期性
        // 目标只会严重衰减（实测目标 ±0.6 rad、弹簧只出 ±0.08 rad，画面上就是
        // "裙子还是没动"）。摆动是**确定性信号**，直接给；二次响应才留给弹簧。
        float gaitAngleX = 0.0f;
        float gaitAngleZ = 0.0f;
        if (tuning.gaitDrive != 0.0f) {
            const glm::vec3 outw = glm::vec3(world[bone][3]) - glm::vec3(world[chain[0]][3]);
            glm::vec3 outh(outw.x, 0.0f, outw.z);
            const float olen = glm::length(outh);
            if (olen > 1.0e-4f) {
                outh /= olen;
                const float sideSign = glm::dot(outh, side) >= 0.0f ? 1.0f : -1.0f;
                const float fwdFactor = glm::dot(outh, fwd);       // +1 正前, -1 正后
                const float sw = (sideSign > 0.0f ? tuning.gaitSwingL : tuning.gaitSwingR);
                // 前片逆着腿的方向往前推、后片跟着往后：用 fwdFactor 加权即可。
                gaitAngleX = tuning.gaitDrive * gain * sw * (0.55f + 0.45f * fwdFactor);
                gaitAngleZ = tuning.gaitDrive * 0.20f * gain * sw * std::abs(sw) * sideSign;
            }
        }

        // ---- 探针拖拽：离该关节最近的几个探针，按接近程度加权 ----
        if (tuning.probeDrag != 0.0f && !legPoints_.empty()) {
            glm::vec3 drag(0.0f);
            float wsum = 0.0f;
            for (std::size_t pi = 0; pi < legPoints_.size(); ++pi) {
                const float d = glm::distance(glm::vec3(world[bone][3]), legPoints_[pi]);
                const float reach = std::max(legRadii_[pi], 1e-3f) * 3.0f;   // 影响半径 = 3 倍探针半径
                const float w = std::clamp(1.0f - d / reach, 0.0f, 1.0f);
                if (w <= 0.0f) continue;
                drag += probeVel[pi] * w;
                wsum += w;
            }
            if (wsum > 1e-4f) drag /= wsum;
            if (std::getenv("GLDX_PROBEDBG")) {
                static int n2 = 0;
                if (n2++ % 20 == 0)
                    std::fprintf(stderr, "[drag] pd=%.3f wsum=%.3f drag=(%.3f,%.3f,%.3f)\n",
                                 (double)tuning.probeDrag, (double)wsum, (double)drag.x,
                                 (double)drag.y, (double)drag.z);
            }
            target.x -= glm::dot(drag, fwd) * tuning.probeDrag * gain;
            target.z -= glm::dot(drag, up) * tuning.probeDrag * 0.6f * gain;
        }

        // ---- 惯性：布面滞后于根的加速度 ----
        //
        // 符号与 `leanFwd` 那条一致地取负：身体往前加速，布面往后甩。
        if (tuning.inertia != 0.0f)
            target.x -= accFwd * tuning.inertia * gain;
        if (tuning.inertiaZ != 0.0f)
            target.z -= accUp * tuning.inertiaZ * gain;

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
            float pushDepth = 0.0f;
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
                pushDepth += depth;
            }
            // 强度必须**正比于穿透深度**。
            //
            // 这里原来写的是 `glm::normalize(push)` 之后再取 `glm::length(push)`——
            // 归一化之后长度恒为 1，于是 mag 恒等于 1：任何一个探针只要进到半径内，
            // 不管只擦到边缘还是深深插进去，都会触发满强度 0.9 rad 的一推。画面上
            // 就是"手/腿一挨到裙摆，整片布猛地鼓开"，紧接着因为布已经鼓走、只剩
            // 弹簧滞后，腿前摆和手后摆就直接从裙面穿出去。
            //
            // 改成累加**深度**而不是累加方向：浅接触给一点点推，深穿透才给满力。
            if (pushDepth > 1e-5f) {
                push = glm::normalize(push);
                // 深度用 sqrt 而不是线性：浅接触也要给一点推，否则"刚好擦到"的腿/手
                // 会让布面完全不动、直接穿过去；深穿透时又能到满力。
                const float mag = std::min(std::sqrt(std::min(pushDepth, 1.0f)) * 1.15f, 1.0f);
                // 上限 2.2 rad：短裙 + 大幅摆腿时，0.9 rad 的布面偏转不足以让下摆
                // 骑到大腿外面，腿就从下摆边缘切出去（实测 walk 相位 0.45 仍有 3.7 cm）。
                target.x += push.x * mag * 2.2f;
                target.z += push.z * mag * 2.2f;
            }
        }

        if (std::getenv("GLDX_SOLVERDBG") && i == 2 && count > 20) {
            static int n = 0;
            if (n++ % 12 == 0)
                std::fprintf(stderr,
                             "[solver] count=%d gd=%.2f tip target=(%+.3f,%+.3f,%+.3f) angle=(%+.3f,%+.3f,%+.3f)\n",
                             count, (double)tuning.gaitDrive,
                             (double)target.x, (double)target.y, (double)target.z,
                             (double)angle_[j].x, (double)angle_[j].y, (double)angle_[j].z);
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
        // 直接叠加的步态角也必须限幅：它绕过了 `StepSpring` 里那道 0.6 rad 的夹紧，
        // 不限的话极端姿态下裙片会翻过去（碗状）。
        const float gx = std::clamp(angle_[j].x + gaitAngleX, -0.6f, 0.6f);
        rot = glm::rotate(rot, gx, glm::vec3(1.0f, 0.0f, 0.0f));
        rot = glm::rotate(rot, angle_[j].y, glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, angle_[j].z + gaitAngleZ, glm::vec3(0.0f, 0.0f, 1.0f));
        local[static_cast<std::size_t>(bone)] = local[static_cast<std::size_t>(bone)] * rot;
    }

    if (advance) {
        prevRootPos_[0] = rootPos;
        prevRootUp_ = rootUp;
    }
}

}  // namespace gldx
