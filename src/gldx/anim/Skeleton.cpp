module;

#include "gldx/gmf.hpp"

#include <cstdio>

module gldx;

namespace gldx {

int Skeleton::Add(Bone bone) {
    if (bone.parent >= static_cast<int>(bones_.size())) {
        // parents-first is the invariant the linear FK sweep keeps; a forward
        // reference would read a world matrix that has not been written yet.
        std::fprintf(stderr, "[Skeleton] bone '%s' references parent %d before it exists; "
                             "treating it as a root\n", bone.name.c_str(), bone.parent);
        bone.parent = -1;
    }
    const int index = static_cast<int>(bones_.size());
    byName_[bone.name] = index;
    bones_.push_back(std::move(bone));
    return index;
}

const Bone* Skeleton::bone(int index) const {
    if (index < 0 || index >= static_cast<int>(bones_.size())) return nullptr;
    return &bones_[static_cast<std::size_t>(index)];
}

int Skeleton::Find(std::string_view name) const {
    const auto it = byName_.find(std::string(name));
    return it == byName_.end() ? -1 : it->second;
}

glm::mat4 Skeleton::RestWorld(int index) const {
    glm::mat4 world(1.0f);
    // An out-of-range index used to walk straight off the end of bones_ and read
    // whatever heap followed it. The usual way to arrive here is a *stale*
    // RigProfile: bone indices are classified against one Skeleton, and if that
    // skeleton is later moved from or replaced after a re-load, the indices still
    // look plausible while naming nothing. Returning identity keeps the frame
    // drawable, and the warning is what makes the real bug findable instead of
    // presenting as a mystery crash inside glm.
    if (index < 0 || index >= static_cast<int>(bones_.size())) {
        std::fprintf(stderr, "[Skeleton] RestWorld(%d): out of range for a %zu-bone rig; "
                             "returning identity (stale bone index?)\n",
                     index, bones_.size());
        return world;
    }
    // Walk to the root and unwrap on the way back; a rig is a handful of bones,
    // so the recursive form costs nothing and keeps this independent of the
    // per-frame scratch space.
    for (int i = index; i >= 0; i = bones_[static_cast<std::size_t>(i)].parent) {
        world = bones_[static_cast<std::size_t>(i)].restLocal * world;
    }
    return world;
}

void Skeleton::ComputeWorld(const std::vector<glm::mat4>& localPose,
                            const glm::mat4& rootTransform,
                            std::vector<glm::mat4>& outWorld) const {
    outWorld.resize(bones_.size());
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        const Bone& b = bones_[i];
        const glm::mat4 local = (i < localPose.size()) ? localPose[i] : b.restLocal;
        outWorld[i] = (b.parent < 0)
                          ? rootTransform * local
                          : outWorld[static_cast<std::size_t>(b.parent)] * local;
    }
}

void Skeleton::ComputeSkinning(const std::vector<glm::mat4>& localPose,
                               const glm::mat4& rootTransform,
                               std::vector<glm::mat4>& outSkinning) const {
    // Local scratch: ComputeWorld writes world matrices and the caller only
    // wants the skinned product, so the intermediate never escapes this call.
    std::vector<glm::mat4> world;
    ComputeWorld(localPose, rootTransform, world);
    outSkinning.resize(bones_.size());
    for (std::size_t i = 0; i < bones_.size(); ++i) {
        outSkinning[i] = world[i] * bones_[i].inverseBind;
    }
}

} // namespace gldx
