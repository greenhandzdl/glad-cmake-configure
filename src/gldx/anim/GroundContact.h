#ifndef GLDX_ANIM_GROUNDCONTACT_H
#define GLDX_ANIM_GROUNDCONTACT_H

/**
 * @file GroundContact.h
 * @brief Solve the pelvis height from where the soles actually are, per frame.
 *
 * Pure CPU math (std + glm), no GL, no Skeleton ownership: buildable and callable
 * on any thread that owns the palette it is handed. It exists because a
 * hand-authored pelvis bob cannot stay in sync with the legs. The failure looks
 * exactly like a bug in the gait curves - the legs scissor, the feet stay flat on
 * the floor, one of them sinks through it at double support - and it is not one:
 * two independently authored numbers were asked to agree frame by frame.
 *
 * So the bob is not authored at all. Four contact points (a heel under each ankle
 * and a ball under each toe) are sampled ONCE from the bind pose, carried by the
 * bones they belong to, and re-measured after posing. Whatever the lowest one sits
 * at is the distance the whole character has to move for a sole to touch y=0, and
 * the pelvis bob falls out of the gait for free: two dips per cycle, deepest at
 * double support.
 *
 * Why the shift goes into the palette rather than into the root transform: a
 * translation prepended to a skinning matrix is the same operation applied once
 * per bone, palette' = T * palette. Shifting the root instead would only move the
 * *rest* pose that every palette entry is computed from, so any caller that had
 * already folded the root in (or that shares a palette across a second draw) would
 * have to undo the shift; shifting the palette entries is exactly the final
 * matrices the shader is about to read.
 *
 * Usage is one frame's worth of calls:
 *   GroundContact ground(skeleton, rigUp, footBone, toeBone, bindLowestVertex);
 *   skinning.ComputeSkinning(pose, root, palette);
 *   const GroundContact::Solution s = ground.Solve(palette);
 *   ground.ApplyLift(palette, s.lift);      // or gate on your own "walking" flag
 *   ground.Bind... / draw with `palette`
 * Note that Solution carries both the lift AND the measured post-solve height:
 * a HUD that prints only the lift cannot tell "the sole is on the floor" from
 * "the solve did nothing", and 0.000 m in a panel is indistinguishable from
 * "not measured".
 */

#include <array>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace gldx {

class Skeleton;

class GroundContact {
public:
    // How many points are sampled: one heel and one ball per foot, two feet.
    // Fixed rather than a vector because it is a rig fact, not a runtime list.
    static constexpr int kMaxContacts = 4;

    // What one frame's solve found.
    struct Solution {
        // Metres to add to the character so its lowest sole lands on y = 0.
        // Zero when no contact could be evaluated (see valid()).
        float lift = 0.0f;

        // Lowest sole height measured AFTER applying `lift`. This is the number a
        // HUD should show next to the lift: it is ~0 when the solve worked and
        // non-zero only when the tolerance below suppressed a sub-millimetre
        // correction.
        float lowestAfter = 0.0f;

        // Index into the palette of the bone that carried the lowest contact, or
        // -1. Useful for diagnostics ("which foot is planted").
        int lowestBone = -1;

        // False when the rig gave no usable contact point at all, in which case
        // `lift` is 0 and the caller should leave the palette alone - shifting by
        // a value that was never solved would slide the character off the floor
        // every frame.
        [[nodiscard]] bool valid() const noexcept { return lowestBone >= 0; }
    };

    GroundContact() = default;

    // Samples the four contact points from the BIND pose. `skeleton` is only used
    // inside this constructor (it is not retained), so the caller stays free to
    // move or destroy it afterwards.
    //
    //   up                 the rig's up axis in rig space. Pass RigProfile::up; it
    //                      is measured from the asset, never assumed, because a
    //                      Z-up FBX and a Y-up glTF differ here and guessing wrong
    //                      lays the character on its side with no error anywhere.
    //   footBone[2]        left/right ankle. -1 skips that contact.
    //   toeBone[2]         left/right toe.  -1 skips that contact.
    //   bindLowestOnUp     the minimum of dot(bindVertex.position, up) over every
    //                      bind-pose vertex, i.e. where the sole plane is. A bone
    //                      span cannot supply this: the ankle joint sits several
    //                      centimetres above the sole, so projecting from the bone
    //                      with dot==0 instead of dot==bindLowestOnUp would float
    //                      the whole character by that offset, silently.
    //
    // Each contact is placed at `bonePos - up * (dot(bonePos, up) - bindLowestOnUp)`,
    // i.e. straight down from the joint onto the sole plane. That is what makes a
    // rotated foot keep a real contact height: the point rides the bone, so when
    // the foot rolls it rolls with it.
    GroundContact(const Skeleton& skeleton, const glm::vec3& up, const int (&footBone)[2],
                  const int (&toeBone)[2], float bindLowestOnUp);

    // Rig-space positions of the sampled contacts (x, y, z in rig space). Exposed
    // for diagnostics only; the solve owns them.
    [[nodiscard]] int contactCount() const noexcept { return contactCount_; }
    [[nodiscard]] glm::vec3 contactPoint(int index) const;
    [[nodiscard]] int contactBone(int index) const;

    // Measures the lowest contact through the palette the caller just computed.
    // `palette` is skinning[i] = world[i] * inverseBind[i], one entry per bone in
    // bone order (that is exactly what Skeleton::ComputeSkinning writes). Nothing
    // is modified. A palette shorter than the highest bone index names is treated
    // as "that contact contributed nothing" rather than read out of bounds.
    [[nodiscard]] Solution Solve(const std::vector<glm::mat4>& palette) const;

    // palette' = T(0, lift, 0) * palette, for every entry.
    //
    // Two things worth stating because both are silent failures otherwise:
    //   * Post-multiplying is wrong. `palette[i] * T` translates in the bone's own
    //     space, so every bone would move along its own local +Y and the character
    //     would shear apart instead of rising; nothing errors, it just deforms.
    //   * The tolerance. A lift under 1e-4 m is NOT applied. Without it the solver
    //     chases the last digit of the trig functions, and on a frozen clock that
    //     shows up as a visible one-pixel shimmer on the feet - reads as "the
    //     ground solve is unstable", when it is measuring its own noise floor.
    //     1e-4 m is well under a millimetre, i.e. below anything the eye can see
    //     at this scale.
    static void ApplyLift(std::vector<glm::mat4>& palette, float lift);

    // Tolerance ApplyLift uses. Public so a caller can report the same number its
    // solve is being held to instead of restating 1e-4.
    static constexpr float kLiftTolerance = 1.0e-4f;

private:
    glm::vec3 contactRig_[kMaxContacts]{};
    int       contactBone_[kMaxContacts] = {-1, -1, -1, -1};
    int       contactCount_ = 0;
};

} // namespace gldx

#endif // GLDX_ANIM_GROUNDCONTACT_H
