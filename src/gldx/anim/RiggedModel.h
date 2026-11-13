#ifndef GLDX_ANIM_RIGGEDMODEL_H
#define GLDX_ANIM_RIGGEDMODEL_H

/**
 * @file RiggedModel.h
 * @brief Stage B owner of one rigged character: skeleton, GPU meshes, maps.
 *
 * The split follows the engine's two-phase asset pipeline. ModelLoader::LoadSkinned
 * is Stage A (worker thread, no GL) and produces LoadedSkinnedModelData; this class
 * is Stage B and owns everything that needs a live GL context:
 *
 *   Skeleton (CPU)  +  one SkinnedMesh per file mesh  +  the decoded maps as
 *   Texture2D  +  the materials  +  the scratch a per-frame palette goes into.
 *
 * Why this exists at all, rather than leaving the plumbing in each demo: three
 * pieces of it are traps that every consumer gets wrong the same way, and getting
 * them wrong is silent.
 *
 *   1. Stand-in maps. A shader that declares `sampler2D uAlbedo` has that sampler
 *      ACTIVE whether or not the file had an albedo map, and Apple's driver
 *      rejects the whole draw call when an active sampler points at an incomplete
 *      or missing texture - the triangles are dropped, GL reports
 *      INVALID_OPERATION, and the visible symptom is "the model vanished" on a
 *      machine where it works everywhere else. So this class always owns three
 *      1x1 dummies (see the accessors below) and a caller binds *something* to
 *      every sampler. It cannot be left to the consumer to remember.
 *   2. Unit scale. A 163-unit character is in centimetres, a 1.63-unit one is
 *      already in metres, and only the file knows which. Guessing wrong puts the
 *      camera inside the model, which reads as "nothing loaded" - indistinguishable
 *      from a failed load, so it gets diagnosed as one.
 *   3. Orientation. The rig's own (side, up, forward) axes are its author's, not
 *      the world's (Z-up FBX vs Y-up glTF). Assuming +Y up lays the character on
 *      its back with no error at all.
 *
 * All of that is derived here, once, at Upload time.
 *
 * The palette is deliberately NOT uploaded per mesh: one skeleton is shared by every
 * mesh of one character, and a caller may draw the same palette through more than
 * one program. BindPalette() therefore takes the program as a parameter and Set()s
 * the pre-generated "uBones[i]" names in one go.
 */

#include <cstddef>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Skeleton.h"
#include "gldx/anim/SkinnedMesh.h"
#include "gldx/assets/ModelLoader.h"
#include "gldx/texture/Texture2D.h"

namespace gldx {

class ShaderProgram;

class RiggedModel {
public:
    RiggedModel() = default;
    ~RiggedModel() = default;                 // members' destructors assert thread affinity

    // Owns GPU resources (SkinnedMesh / Texture2D), so it follows the same
    // move-only RAII rule as everything else in core: one GL id, one owner.
    RiggedModel(const RiggedModel&)            = delete;
    RiggedModel& operator=(const RiggedModel&) = delete;
    RiggedModel(RiggedModel&&) noexcept            = default;
    RiggedModel& operator=(RiggedModel&&) noexcept = default;

    // Stage B: create every GL object from the worker's result. Render-thread only.
    //
    // Takes the data by rvalue reference because the vertex data is moved into the
    // VBOs and the CPU copies are then dropped - a rigged character is tens of
    // megabytes, so keeping the Stage A buffers alive would double that for nothing.
    //
    // Afterwards ready() tells whether anything arrived. A model that uploaded 0
    // meshes is not an error to throw over: the caller may still want the
    // diagnostics (sourcePath, skeleton bone count) to print.
    //
    // `rig` is optional and only exists to avoid classifying twice: the caller has
    // usually built a RigProfile already (it needs one for SetOrientation and for
    // its own gait), and the leg-length measurement below needs one too. Passing it
    // in keeps this class from growing a second classification pass - and, less
    // obviously, keeps Upload's cost from depending on how many times a caller
    // looked at the rig. Pass std::nullopt (or omit) and Upload classifies once
    // itself; that is correct, just not free.
    void Upload(LoadedSkinnedModelData&& data, const RigProfile* rig = nullptr);

    [[nodiscard]] bool ready() const noexcept { return !meshes_.empty(); }
    [[nodiscard]] const std::string& sourcePath() const noexcept { return sourcePath_; }

    // ---- geometry ---------------------------------------------------------
    [[nodiscard]] const Skeleton& skeleton() const noexcept { return skeleton_; }
    [[nodiscard]] const std::vector<SkinnedMesh>& meshes() const noexcept { return meshes_; }

    [[nodiscard]] std::size_t vertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] std::size_t triangleCount() const noexcept { return triangleCount_; }

    // ---- derived rig facts (all computed in Upload) -----------------------
    // Rig units -> metres. 0.01 for a centimetre-scaled file, 1 for a metre one.
    [[nodiscard]] float unitScale() const noexcept { return unitScale_; }

    // Rig axes -> world basis, column-ordered (side, up, forward). Multiply a rig
    // vector by this to get a world one; Mat4() folds it with unitScale below.
    [[nodiscard]] const glm::mat3& orientation() const noexcept { return orientation_; }

    // Hands the rig's measured basis to the model. Upload() sets the identity
    // because a (side, up, forward) triple can only come from rig classification,
    // which lives in RigProfile and must not be duplicated here. Call this once
    // after Upload with RigProfile's triple:
    //     model.SetOrientation(glm::mat3(rig.side, rig.up, rig.forward));
    // Skipping it is not an error and not a crash: the character simply stands
    // along the rig's own axes, which for a Z-up FBX is on its back.
    void SetOrientation(const glm::mat3& orientation);

    // Same, from the triple directly. Does not normalise or orthonormalise - a
    // non-orthonormal basis silently shears the character, so the producer is
    // expected to hand over three unit, mutually perpendicular axes.
    void SetOrientation(const glm::vec3& side, const glm::vec3& up, const glm::vec3& forward);

    // The root transform that turns rig space into metres in this scene's world:
    // scale by unitScale, then rotate onto the world basis. This is what a caller
    // passes to Skeleton::ComputeSkinning as `rootTransform`; writing it again at
    // the call site is how two demos end up disagreeing about which axis is up.
    [[nodiscard]] glm::mat4 Mat4() const;

    // Character height along the rig's up axis, in metres (>= 0.2 guard applied).
    [[nodiscard]] float heightMetres() const noexcept { return heightMetres_; }

    // Hip-to-ankle distance in metres, or a 0.52 * height estimate when the rig
    // gave no usable leg chain. A walk cycle's stride and cadence both come from
    // this number, so a wrong one makes the feet skate.
    [[nodiscard]] float legLengthMetres() const noexcept { return legLengthMetres_; }

    // Largest edge over every uploaded map, for a diagnostics line ("maps up to
    // 2048x2048"). 0 when the file carried no usable map.
    [[nodiscard]] int textureWidth() const noexcept { return textureWidth_; }
    [[nodiscard]] int textureHeight() const noexcept { return textureHeight_; }

    // Map references the loader could not resolve to bytes (a path outside the
    // model's directory, or a file that is not there). Kept rather than dropped:
    // these are what distinguish "this file genuinely has no normal map" from
    // "this file names a normal map that failed to load", and the two want
    // different fixes. The four has*() flags cannot tell them apart, because both
    // end up false.
    [[nodiscard]] const std::vector<std::string>& missingTexturePaths() const noexcept {
        return missingTexturePaths_;
    }

    // ---- maps -------------------------------------------------------------
    // one per entry of the loader's texture list. An entry may be INVALID when the
    // file referenced a map whose bytes could not be decoded; that is normal and
    // the fallbacks below are what a draw should use for it.
    [[nodiscard]] const std::vector<Texture2D>& textures() const noexcept { return textures_; }

    // The 1x1 stand-ins (see the file comment). Always valid after Upload.
    //   white albedo  - sRGB white, so the material's base colour shows unchanged
    //   flat normal   - (128,128,255), a +Z normal, so the cotangent frame is a
    //                   no-op instead of perturbing a surface that has no map
    //   white scalar  - linear white, so the metallic/roughness factors pass through
    [[nodiscard]] const Texture2D& fallbackAlbedo() const noexcept { return fallbackAlbedo_; }
    [[nodiscard]] const Texture2D& fallbackNormal() const noexcept { return fallbackNormal_; }
    [[nodiscard]] const Texture2D& fallbackScalar() const noexcept { return fallbackScalar_; }

    // Resolves one sampler slot to a texture that is guaranteed complete: the real
    // map when the loader supplied usable bytes, the matching fallback otherwise.
    // This is the accessor a draw loop should call - it is the answer to trap (1)
    // and it costs one branch.
    [[nodiscard]] const Texture2D& AlbedoOrFallback() const;
    [[nodiscard]] const Texture2D& NormalOrFallback() const;
    [[nodiscard]] const Texture2D& MetallicOrFallback() const;
    [[nodiscard]] const Texture2D& RoughnessOrFallback() const;

    // ---- materials --------------------------------------------------------
    // The whole list as loaded, plus the per-mesh index into it. Kept as vectors
    // rather than collapsed to one material because a multi-part FBX legitimately
    // carries several, and collapsing would silently repaint the parts that
    // disagreed with part 0.
    [[nodiscard]] const std::vector<SkinnedMaterial>& materials() const noexcept { return materials_; }
    [[nodiscard]] const std::vector<int>& meshMaterial() const noexcept { return meshMaterial_; }

    // The material one mesh draws with, or nullptr when the file carried none.
    // Out-of-range mesh indices and missing material indices both return nullptr
    // rather than indexing the vector.
    [[nodiscard]] const SkinnedMaterial* MaterialFor(std::size_t mesh) const;

    // Convenience for the common single-material case: materials().front(), or a
    // default-constructed one when the list is empty (so a caller can read
    // baseColor without a null check). MaterialFor() is the honest accessor.
    [[nodiscard]] SkinnedMaterial PrimaryMaterial() const;

    // True when the file actually supplied bytes for that role *and* the upload
    // produced a valid GL texture. False means a draw should use the fallback.
    // (An entry the loader listed but failed to decode is false here but would
    // still occupy an index in textures(), which is why this is not just a range
    // check.)
    [[nodiscard]] bool hasAlbedo() const noexcept { return hasAlbedo_; }
    [[nodiscard]] bool hasNormal() const noexcept { return hasNormal_; }
    [[nodiscard]] bool hasMetallic() const noexcept { return hasMetallic_; }
    [[nodiscard]] bool hasRoughness() const noexcept { return hasRoughness_; }

    // ---- pose plumbing ----------------------------------------------------
    // "uBones[0]".."uBones[N-1]", built once at Upload. Pre-generating these is the
    // whole reason a caller does not have to know the palette convention: the name
    // format and kMaxBonePalette have to match the GLSL array declaration exactly,
    // and a typo there is a uniform that silently never gets set.
    [[nodiscard]] const std::vector<std::string>& paletteNames() const noexcept {
        return paletteNames_;
    }

    // Rest-pose world matrix per bone, cached from Skeleton::RestWorld. The cache
    // exists because it is queried every frame by the pose layer (a world-space
    // rotation axis has to be mapped into each bone's local frame) and RestWorld is
    // a root-to-leaf walk with a matrix multiply per level.
    [[nodiscard]] const std::vector<glm::mat4>& restWorld() const noexcept { return restWorld_; }

    // Per-frame scratch, owned here so a caller does not have to allocate its own
    // and then hand it back for every call. These are the same two buffers the
    // demo used to keep in its `Character` struct: `pose` is bone-local matrices
    // (what an animation layer writes), `palette` is skinning[i] = world*inverseBind
    // (what the shader and GroundContact read).
    [[nodiscard]] const std::vector<glm::mat4>& pose() const noexcept { return pose_; }
    [[nodiscard]] const std::vector<glm::mat4>& palette() const noexcept { return palette_; }

    // Mutable palette accessor, for GroundContact::ApplyLift - the ground solve
    // works by translating the final skinning matrices, so it has to write them.
    // Handing out a mutable reference is the honest way to express that; the
    // alternative (a setter taking the shifted copy) would copy N matrices per
    // frame for no reason.
    [[nodiscard]] std::vector<glm::mat4>& palette() noexcept { return palette_; }

    // Forward kinematics for one frame: `localPose` is bone-local matrices in bone
    // order, and may be shorter than the skeleton (the missing tail keeps its rest
    // transform) or empty (the whole rig sits at rest). Fills pose() and palette().
    //
    // The root transform is Mat4(), not a caller argument: the palette has to be in
    // metres on the world basis for GroundContact (which measures in world metres)
    // and for the shader alike. Letting the caller pass a different root here would
    // be the one place the two could disagree.
    void ComputePalette(const std::vector<glm::mat4>& localPose);

    // Uploads palette() into `program` as uBones[0..n-1], n = skeleton().size().
    //
    // No Use() call, and that is not an oversight: ShaderProgram::Set is the
    // glProgramUniform* family, which addresses the program by id, so it does not
    // need (and must not depend on) whatever program happens to be bound. That is
    // what lets one palette be shared by a skinned pass and a shadow/depth pass
    // that has no use for the bones.
    //
    // Render-thread only (it calls into GL). A program that lacks the uniform array
    // is not an error: Set() on a missing location is a no-op, which is why this is
    // safe to call on every program a character is drawn with.
    void BindPalette(const ShaderProgram& program) const;

    // Same, for a palette the caller built itself (a second draw with a different
    // root, say). The names still come from this object so they cannot drift from
    // the GLSL array declaration.
    void BindPalette(const ShaderProgram& program, const std::vector<glm::mat4>& palette) const;

    // The bind-pose self-check: with an empty pose, every skinning matrix must be
    // exactly Mat4(). It is structural (restLocal is derived from these same
    // inverse-bind matrices) but it is the property every walk cycle rests on, so it
    // is measured rather than assumed. max |element difference|, reported by demos
    // as one number; anything above ~1e-6 means the rig and its bind pose disagree
    // and the character will be deformed before it is ever animated.
    [[nodiscard]] float bindDeviation() const noexcept { return bindDeviation_; }

private:
    Skeleton                 skeleton_;
    std::vector<SkinnedMesh> meshes_;
    std::vector<Texture2D>   textures_;
    Texture2D                fallbackAlbedo_;
    Texture2D                fallbackNormal_;
    Texture2D                fallbackScalar_;
    std::vector<SkinnedMaterial> materials_;
    std::vector<int>         meshMaterial_;

    std::vector<glm::mat4>   restWorld_;
    std::vector<glm::mat4>   pose_;
    std::vector<glm::mat4>   palette_;
    std::vector<std::string> paletteNames_;
    std::vector<std::string> missingTexturePaths_;

    std::string sourcePath_;
    glm::mat3   orientation_{1.0f};
    float       unitScale_        = 1.0f;
    float       heightMetres_     = 1.7f;
    float       legLengthMetres_  = 0.9f;
    float       bindDeviation_    = 0.0f;
    std::size_t vertexCount_      = 0;
    std::size_t triangleCount_    = 0;
    int         textureWidth_     = 0;
    int         textureHeight_    = 0;
    bool        hasAlbedo_        = false;
    bool        hasNormal_        = false;
    bool        hasMetallic_      = false;
    bool        hasRoughness_     = false;
};

} // namespace gldx

#endif // GLDX_ANIM_RIGGEDMODEL_H
