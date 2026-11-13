module;

#include "gldx/gmf.hpp"

module gldx;

// Skeleton is only forward-declared in this class's header - deliberately, since
// GroundContact does not retain one. Its definition arrives through the module
// itself (gldx.cppm exports Skeleton.h), which is the same route SkinnedMesh.cpp
// uses for RenderContext. Do NOT textually include Skeleton.h again here: it is
// already part of the exported module and a second include site is a redefinition.

namespace gldx {

GroundContact::GroundContact(const Skeleton& skeleton, const glm::vec3& up,
                             const int (&footBone)[2], const int (&toeBone)[2],
                             float bindLowestOnUp) {
    // A degenerate up axis would project every contact onto the same point and
    // produce a lift of 0 forever - the character would look like it is walking in
    // mid-air and nothing would report an error. Falls back to +Y, which is the
    // axis every exporter's "default" orientation uses anyway.
    //
    // Normalise ONLY when the caller's axis is not already unit length. That is not
    // a micro-optimisation: `up` almost always arrives from RigProfile, which has
    // already normalised it, and normalising an already-normalised float vector is
    // NOT the identity - `v / |v|` re-rounds every component, so every contact point
    // moves by a fraction of an LSB. Harmless in isolation, but it shifts the whole
    // character by ±1/255 across a couple of hundred pixels, which is exactly the
    // noise that makes an A/B snapshot comparison unreadable. A caller that passes a
    // raw un-normalised axis still gets it normalised here.
    const float upLen2 = glm::dot(up, up);
    glm::vec3 axis(0.0f, 1.0f, 0.0f);
    if (upLen2 > 1.0e-8f) {
        axis = (glm::abs(upLen2 - 1.0f) > 1.0e-6f) ? up / std::sqrt(upLen2) : up;
    }

    auto addContact = [&](int bone) {
        if (bone < 0 || contactCount_ >= kMaxContacts) return;
        const glm::mat4 rest = skeleton.RestWorld(bone);
        const glm::vec3 bonePos(rest[3]);
        // Drop the joint straight down onto the sole plane. `bindLowestOnUp` is the
        // lowest bind vertex's projection on `axis`, so the offset
        // (dot(bonePos, axis) - bindLowestOnUp) is exactly how far the joint sits
        // above the floor in the bind pose. Using 0 here instead would put the
        // contact at the joint itself, and the whole character would hover by the
        // ankle height - several centimetres, no warning.
        contactRig_[contactCount_] =
            bonePos - axis * (glm::dot(bonePos, axis) - bindLowestOnUp);
        contactBone_[contactCount_] = bone;
        ++contactCount_;
    };

    // Order is the demo's original: left heel, left ball, right heel, right ball.
    // Kept because the gait report prints per-side rows, and a reorder would move
    // which row is which without changing any number.
    addContact(footBone[0]);
    addContact(toeBone[0]);
    addContact(footBone[1]);
    addContact(toeBone[1]);
}

glm::vec3 GroundContact::contactPoint(int index) const {
    if (index < 0 || index >= contactCount_) return glm::vec3(0.0f);
    return contactRig_[static_cast<std::size_t>(index)];
}

int GroundContact::contactBone(int index) const {
    if (index < 0 || index >= contactCount_) return -1;
    return contactBone_[static_cast<std::size_t>(index)];
}

GroundContact::Solution GroundContact::Solve(const std::vector<glm::mat4>& palette) const {
    Solution result;
    float lowest = 0.0f;
    bool have = false;

    for (int c = 0; c < contactCount_; ++c) {
        const int bone = contactBone_[static_cast<std::size_t>(c)];
        if (bone < 0) continue;
        // A palette that does not reach this bone means the caller posed a
        // different skeleton than the one this object was built from. Skip it
        // instead of indexing past the end: the remaining contacts still produce a
        // usable lift, and a partially solved height is far better than a crash in
        // a frame callback.
        if (static_cast<std::size_t>(bone) >= palette.size()) continue;
        const glm::vec4 p =
            palette[static_cast<std::size_t>(bone)] *
            glm::vec4(contactRig_[static_cast<std::size_t>(c)], 1.0f);
        if (!have || p.y < lowest) {
            lowest = p.y;
            result.lowestBone = bone;
            have = true;
        }
    }

    if (!have) return result;   // valid() == false, lift stays 0

    // Negative lift would mean the soles are already above the floor (a pose that
    // lifts both feet); the solve is symmetric, so the character gets pulled DOWN
    // to touch. That is deliberate: floating is as wrong as sinking.
    result.lift = -lowest;
    // The post-solve height is reported as "lowest after the shift that ApplyLift
    // would make", including the tolerance interaction: when the lift is under the
    // tolerance nothing is applied, so the sole really is still at `lowest`. A
    // HUD that showed 0.0 in that case would be claiming a correction happened.
    const float applied = (glm::abs(result.lift) > kLiftTolerance) ? result.lift : 0.0f;
    result.lowestAfter = lowest + applied;
    return result;
}

void GroundContact::ApplyLift(std::vector<glm::mat4>& palette, float lift) {
    if (!(glm::abs(lift) > kLiftTolerance)) return;
    // m[3] is the translation column of a column-major glm::mat4, and for a matrix
    // built as world * inverseBind it is the bone's posed position. Adding to its Y
    // IS prepending T(0, lift, 0): for a column-major matrix M, (T * M)[3] =
    // M[3] + (0, lift, 0), and the upper 3x3 is untouched because T has an identity
    // rotation. So this fast path is the whole operation, not an approximation of
    // it - the full glm::translate() product would compute the same numbers.
    for (glm::mat4& m : palette) {
        m[3].y += lift;
    }
}

} // namespace gldx
