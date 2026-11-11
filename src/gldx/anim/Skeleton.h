#ifndef GLDX_ANIM_SKELETON_H
#define GLDX_ANIM_SKELETON_H

/**
 * @file Skeleton.h
 * @brief CPU-side bone hierarchy and the linear-blend-skinning transform math.
 *
 * Pure math (std + glm): buildable on a worker thread, no GL, no Assimp. The
 * ModelLoader produces a Skeleton from a rigged file (Stage A); the render
 * thread only consumes the per-frame palette this class computes, so the same
 * Skeleton value type can be moved between stages like any other asset data.
 *
 * The two matrices a Bone carries are the pair the skinning equation is written
 * with, and they answer different questions:
 *   - inverseBind maps a bind-pose vertex from mesh space into the bone's space
 *     (Assimp's aiBone::mOffsetMatrix). It never changes for an asset.
 *   - restLocal is the bone's local transform in the bind pose, *derived* from
 *     inverseBind and the parent chain. The animation layer reads it as the pose
 *     to return to, and every procedural clip is authored as a delta around it.
 *
 * Why restLocal is derived from the bind matrices rather than from the file's
 * node transforms: on the FBX asset this class was written for, Assimp's node
 * rotation for a bone and its cluster's bind matrix disagree (the node reports
 * the animated local transform, the cluster the bind-space one), so building
 * the rest pose from the node tree deforms the mesh before anything is animated.
 * Deriving it from the same matrices the shader blends makes "pose == bind pose"
 * an identity by construction, which is exactly the property a walk cycle needs
 * to start from.
 */

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace gldx {

// Palette ceiling shared by loader and shader: a skin is uploaded as a
// `uniform mat4 uBones[N]` array, and GLSL needs a literal size. The loader
// refuses a rig above this and the vertex shader declares the same number, so
// the two halves cannot drift apart silently. 64 covers the humanoid rigs this
// engine's demos use with room to spare; raise it and the GLSL together.
inline constexpr int kMaxBonePalette = 64;

struct Bone {
    std::string name;
    int         parent = -1;              // index into Skeleton::bones(); -1 = root
    glm::mat4   inverseBind{1.0f};        // mesh -> bone space (never animated)
    glm::mat4   restLocal{1.0f};          // bone-local bind transform (parent -> this)
};

class Skeleton {
public:
    Skeleton() = default;

    // Appends a bone; `bone.parent` must already name an existing index (bones
    // are stored parents-first so the whole FK pass is one linear sweep). A
    // parent that does not exist yet is treated as "root" and reported, because
    // silently keeping it would make the sweep read an uninitialised parent.
    int Add(Bone bone);

    [[nodiscard]] std::size_t size() const noexcept { return bones_.size(); }
    [[nodiscard]] bool empty() const noexcept { return bones_.empty(); }
    [[nodiscard]] const std::vector<Bone>& bones() const noexcept { return bones_; }
    [[nodiscard]] const Bone* bone(int index) const;
    [[nodiscard]] int Find(std::string_view name) const;

    // Rest-pose world transform of one bone (bind space, before the caller's
    // root transform). This is what an animation layer converts a world-space
    // rotation axis against - the bones' own axes come from the rig, so a clip
    // has to state its swings in a space it can reason about and let this map
    // them in: axisLocal = inverse(mat3(RestWorld(i))) * worldAxis.
    [[nodiscard]] glm::mat4 RestWorld(int index) const;

    // Forward kinematics. `localPose` may be shorter than the skeleton (the
    // missing tail keeps its rest transform) or empty (the whole rig is at
    // rest). Writes one world matrix per bone, in bone order.
    void ComputeWorld(const std::vector<glm::mat4>& localPose, const glm::mat4& rootTransform,
                      std::vector<glm::mat4>& outWorld) const;

    // What the vertex shader needs: skin[i] = world[i] * inverseBind[i], i.e.
    // the matrix that takes a bind-space vertex to its posed position. With an
    // empty pose every entry is the root transform, so a skinned mesh drawn with
    // it is bit-for-bit its bind pose - the property the demo's `--off walk`
    // frame relies on. Render-thread consumption is read-only: safe to call on
    // any thread that owns these value types.
    void ComputeSkinning(const std::vector<glm::mat4>& localPose, const glm::mat4& rootTransform,
                         std::vector<glm::mat4>& outSkinning) const;

private:
    std::vector<Bone> bones_;
    std::unordered_map<std::string, int> byName_;
};

} // namespace gldx

#endif // GLDX_ANIM_SKELETON_H
