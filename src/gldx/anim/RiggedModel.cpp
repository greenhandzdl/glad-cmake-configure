module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

namespace {

// One texel, as a 1x1 RGBA8 descriptor. `srgb` is per-role and not a formatting
// detail: an albedo map is sRGB-encoded colour and gets linearised on sample, while
// a normal or a scalar map holds raw numbers that must NOT be transformed. Marking
// the flat normal as sRGB would drag (128,128,255) through a gamma decode to a
// completely different vector, and every lit surface would tilt.
Texture2DDesc MakeSolidDesc(unsigned char r = 255, unsigned char g = 255,
                            unsigned char b = 255, unsigned char a = 255, bool srgb = false) {
    Texture2DDesc desc;
    desc.width    = 1;
    desc.height   = 1;
    desc.channels = 4;
    desc.srgb     = srgb;
    desc.pixels   = {r, g, b, a};
    return desc;
}

// Rest-pose world-space position of a bone, or the origin for -1.
glm::vec3 BonePosition(const Skeleton& skeleton, int bone) {
    return bone < 0 ? glm::vec3(0.0f) : glm::vec3(skeleton.RestWorld(bone)[3]);
}

} // namespace

void RiggedModel::Upload(LoadedSkinnedModelData&& data, const RigProfile* rig) {
    RenderContext::AssertRenderThread("RiggedModel::Upload");

    skeleton_   = std::move(data.skeleton);
    sourcePath_ = std::move(data.sourcePath);
    materials_  = std::move(data.materials);
    meshMaterial_ = std::move(data.meshMaterial);
    // Kept for the caller's diagnostics: "no map" and "a map that failed to
    // load" both leave has*() false, and only this list tells them apart.
    missingTexturePaths_ = std::move(data.missingTexturePaths);

    // ---- extent and unit scale -------------------------------------------
    // Measured along the rig's up axis from the BIND-POSE VERTICES themselves, not
    // from the bone span: a bone span misses both the top of the head and the soles
    // of the feet, which on a humanoid is a good 10% of the height.
    //
    // The up axis here is +Y in rig space. RiggedModel deliberately does not own rig
    // *classification* (which bone is the hip, which way is up for THIS file) - that
    // is RigProfile's job, and the two must not both claim it. The consequence to
    // know: unitScale is a scale estimate, and +Y is the axis a metre/cm decision is
    // made against. A Z-up file gets a slightly different extent from this measure,
    // which only matters if its height happens to sit near the 20-unit threshold.
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    float minUp = 0.0f;
    float maxUp = 0.0f;
    bool  first = true;
    for (const SkinnedMeshData& mesh : data.meshes) {
        for (const SkinnedVertex& v : mesh.vertices) {
            const float d = glm::dot(v.position, up);
            if (first) {
                minUp = maxUp = d;
                first = false;
            }
            minUp = std::min(minUp, d);
            maxUp = std::max(maxUp, d);
        }
        vertexCount_   += mesh.vertices.size();
        triangleCount_ += mesh.indices.size() / 3;
    }

    const float rigHeight = first ? 1.0f : (maxUp - minUp);
    // Units are a property of the file, not a knob. A 163-unit character is in
    // centimetres; a 1.63-unit one is already in metres. Guessing wrong is silent:
    // the camera ends up inside the model and the frame reads as "nothing loaded",
    // which gets debugged as a failed load rather than as a units bug.
    unitScale_ = (rigHeight > 20.0f) ? 0.01f : 1.0f;
    // The 0.2 m floor exists so a degenerate / empty file does not divide by ~0 in
    // the caller's camera framing; it is a guard, not a measurement.
    heightMetres_ = std::max(rigHeight * unitScale_, 0.2f);

    // ---- orientation -----------------------------------------------------
    // The identity until the caller supplies one, because this class must not guess
    // (side, up, forward) from bone names - that is RigProfile's job, and two
    // independent classifications is how a rig half-binds. A caller that has one
    // assigns it with SetOrientation().
    //
    // Reset BEFORE the first ComputeSkinning below: Mat4() reads this member, and a
    // second Upload() onto the same object would otherwise build the initial palette
    // from the PREVIOUS character's orientation - a stale rotation on a fresh rig,
    // with nothing in the logs to say so.
    orientation_ = glm::mat3(1.0f);

    // ---- rest-world cache and the palette names --------------------------
    restWorld_.resize(skeleton_.size());
    // Pose/palette scratch sized once: these two are rewritten every frame, and a
    // resize on the render thread is exactly the per-frame allocation the two-phase
    // design exists to avoid. `pose_` starts as the rest pose so a caller that never
    // calls ComputePalette() still has an honest "rest" answer.
    //
    // The initial palette is the rest palette computed the same way
    // Skeleton::ComputeSkinning would: world * inverseBind with the same root, NOT a
    // hand-rolled restWorld * inverseBind. Those two differ by exactly the root
    // transform (scale + orientation), so the hand-rolled version would leave a
    // character drawn before its first ComputePalette() a hundred times too large or
    // too small on a centimetre asset - the same silent-units trap as unitScale,
    // reintroduced at the other end.
    pose_.resize(skeleton_.size());
    paletteNames_.clear();
    paletteNames_.reserve(skeleton_.size());
    for (std::size_t i = 0; i < skeleton_.size(); ++i) {
        restWorld_[i] = skeleton_.RestWorld(static_cast<int>(i));
        pose_[i]      = skeleton_.bones()[i].restLocal;
        // "uBones[i]" has to match the GLSL `uniform mat4 uBones[kMaxBonePalette]`
        // declaration character for character. It is built here once so the format
        // string exists in exactly one place; a name that does not match is a
        // uniform that silently never gets set, and the symptom is a mesh frozen in
        // its bind pose while the skeleton animates correctly on the CPU.
        paletteNames_.push_back("uBones[" + std::to_string(i) + "]");
    }
    skeleton_.ComputeSkinning({}, Mat4(), palette_);

    // ---- embedded maps (Stage B: GL objects, render thread) --------------
    textures_.resize(data.textures.size());
    for (std::size_t i = 0; i < data.textures.size(); ++i) {
        const Texture2DDesc& desc = data.textures[i].desc;
        // A role the loader listed but could not decode arrives with empty pixels.
        // Skipping it leaves textures_[i] invalid, which the *OrFallback() accessors
        // below turn into the dummy - the draw still has a complete texture.
        if (desc.width <= 0 || desc.height <= 0 || desc.pixels.empty()) continue;
        textures_[i].Upload(desc);
        textureWidth_  = std::max(textureWidth_, desc.width);
        textureHeight_ = std::max(textureHeight_, desc.height);
    }

    // Which sampler slots actually have a complete texture behind them. This asks
    // the GL object, not the loader: an entry can survive the loader and still fail
    // Upload if its dimensions were out of bounds, and the shader's `uHasAlbedo`
    // flag has to agree with what is bound.
    //
    // A local value, not a reference into materials_: binding a temporary to a
    // const& in the empty case would outlive nothing and read freed memory below.
    const SkinnedMaterial primary = materials_.empty() ? SkinnedMaterial{} : materials_.front();
    auto usable = [&](int index) {
        return index >= 0 && static_cast<std::size_t>(index) < textures_.size()
               && textures_[static_cast<std::size_t>(index)].valid();
    };
    hasAlbedo_    = usable(primary.albedoTexture);
    hasNormal_    = usable(primary.normalTexture);
    hasMetallic_  = usable(primary.metallicTexture);
    hasRoughness_ = usable(primary.roughnessTexture);

    // ---- leg length ------------------------------------------------------
    // Hip to ankle in metres. Measured only when the rig names the pair, because
    // "hip" and "ankle" are exporter conventions, not rig facts: full name
    // classification is RigProfile's job and this class must not grow a second copy
    // of that table (two tables that disagree is how a rig silently half-binds).
    // Without the names the humanoid proportion 0.52 * height is used, which is a
    // visible approximation rather than a safe default - this number feeds stride
    // length and cadence, so a rig at a very different proportion skates its feet.
    {
        // Classify through RigProfile rather than `Find("Hips")` etc. This class must
        // not look names up itself: the paragraph above says so, and the first version
        // of this block broke its own rule (it hardcoded "Hips"/"LeftFoot"/"Foot.L"),
        // which meant every non-Mixamo rig - a Hunyuan asset, say - silently fell back
        // to the 0.52 * height guess with no diagnostic at all. Going through
        // RigProfile instead means a new naming family is picked up here for free, and
        // the only way to get the fallback is a rig that genuinely has no leg chain.
        // Reuse the caller's classification when it handed one over; classifying
        // again here would be the same answer computed twice (and, on a demo whose
        // first frame is timing-sensitive, two extra passes over a 23-bone rig).
        RigProfile local;
        if (rig == nullptr) {
            local = RigProfile::Classify(skeleton_);
            rig = &local;
        }
        const int hips = rig->hips;
        const int ankle = rig->foot[kRigLeft];
        if (hips >= 0 && ankle >= 0) {
            const float span = glm::length(BonePosition(skeleton_, hips)
                                           - BonePosition(skeleton_, ankle)) * unitScale_;
            legLengthMetres_ = span;
        }
        if (!(legLengthMetres_ > 1e-3f)) legLengthMetres_ = heightMetres_ * 0.52f;
    }

    // ---- GPU meshes ------------------------------------------------------
    meshes_.resize(data.meshes.size());
    for (std::size_t i = 0; i < data.meshes.size(); ++i) {
        meshes_[i].Upload(std::move(data.meshes[i]));
    }

    // ---- the 1x1 dummies -------------------------------------------------
    // Uploaded LAST so they are the most recent texture created: if the context is
    // ever shared with something that leaves a sampler bound, the last thing bound
    // here is still a complete 1x1 texture rather than one of the huge character
    // maps. The order is belt-and-braces, not a correctness requirement.
    fallbackAlbedo_.Upload(MakeSolidDesc(255, 255, 255, 255, /*srgb=*/true));
    fallbackNormal_.Upload(MakeSolidDesc(128, 128, 255));
    fallbackScalar_.Upload(MakeSolidDesc());

    // ---- bind self-check -------------------------------------------------
    // With an empty pose every skinning matrix must equal Mat4() exactly. This is
    // the assumption a walk cycle starts from ("rest pose == bind pose"), and it is
    // checked rather than assumed because the alternative - a rig whose node
    // transforms and cluster bind matrices disagree - deforms the mesh before any
    // animation runs, and looks like an animation bug.
    {
        std::vector<glm::mat4> check;
        const glm::mat4 root = Mat4();
        skeleton_.ComputeSkinning(std::vector<glm::mat4>{}, root, check);
        float deviation = 0.0f;
        for (const glm::mat4& m : check) {
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    deviation = std::max(deviation, std::abs(m[c][r] - root[c][r]));
                }
            }
        }
        bindDeviation_ = deviation;
    }
}

void RiggedModel::SetOrientation(const glm::mat3& orientation) {
    orientation_ = orientation;
}

void RiggedModel::SetOrientation(const glm::vec3& side, const glm::vec3& up,
                                 const glm::vec3& forward) {
    // Columns, in that order: rig axes -> world. glm::mat3(a, b, c) takes vectors as
    // columns, which is what makes `orientation * v` rotate a rig vector into world.
    orientation_ = glm::mat3(side, up, forward);
}

glm::mat4 RiggedModel::Mat4() const {
    // Rotate onto the world basis, then scale. Uniform scale commutes with rotation,
    // so the order is about readability, not correctness - but it stays correct if a
    // non-uniform scale ever arrives.
    return glm::mat4(orientation_) * glm::scale(glm::mat4(1.0f), glm::vec3(unitScale_));
}

const Texture2D& RiggedModel::AlbedoOrFallback() const {
    if (hasAlbedo_) {
        const int index = materials_.front().albedoTexture;
        if (index >= 0 && static_cast<std::size_t>(index) < textures_.size()) {
            return textures_[static_cast<std::size_t>(index)];
        }
    }
    return fallbackAlbedo_;
}

const Texture2D& RiggedModel::NormalOrFallback() const {
    if (hasNormal_) {
        const int index = materials_.front().normalTexture;
        if (index >= 0 && static_cast<std::size_t>(index) < textures_.size()) {
            return textures_[static_cast<std::size_t>(index)];
        }
    }
    return fallbackNormal_;
}

const Texture2D& RiggedModel::MetallicOrFallback() const {
    if (hasMetallic_) {
        const int index = materials_.front().metallicTexture;
        if (index >= 0 && static_cast<std::size_t>(index) < textures_.size()) {
            return textures_[static_cast<std::size_t>(index)];
        }
    }
    return fallbackScalar_;
}

const Texture2D& RiggedModel::RoughnessOrFallback() const {
    if (hasRoughness_) {
        const int index = materials_.front().roughnessTexture;
        if (index >= 0 && static_cast<std::size_t>(index) < textures_.size()) {
            return textures_[static_cast<std::size_t>(index)];
        }
    }
    return fallbackScalar_;
}

const SkinnedMaterial* RiggedModel::MaterialFor(std::size_t mesh) const {
    if (mesh >= meshMaterial_.size()) return nullptr;
    const int index = meshMaterial_[mesh];
    if (index < 0 || static_cast<std::size_t>(index) >= materials_.size()) return nullptr;
    return &materials_[static_cast<std::size_t>(index)];
}

SkinnedMaterial RiggedModel::PrimaryMaterial() const {
    // A value copy, not a reference: the empty case has nothing to point at, and
    // returning a reference to a temp is the kind of bug this API shape invites.
    return materials_.empty() ? SkinnedMaterial{} : materials_.front();
}

void RiggedModel::ComputePalette(const std::vector<glm::mat4>& localPose) {
    skeleton_.ComputeSkinning(localPose, Mat4(), palette_);
    // pose() mirrors what was asked for rather than the palette, so a caller can
    // read back "what pose am I in" without re-deriving it: an empty input means
    // "rest", which is restLocal per bone, not an empty vector.
    pose_.resize(skeleton_.size());
    for (std::size_t i = 0; i < skeleton_.size(); ++i) {
        pose_[i] = (i < localPose.size()) ? localPose[i] : skeleton_.bones()[i].restLocal;
    }
}

void RiggedModel::BindPalette(const ShaderProgram& program) const {
    BindPalette(program, palette_);
}

void RiggedModel::BindPalette(const ShaderProgram& program,
                              const std::vector<glm::mat4>& palette) const {
    RenderContext::AssertRenderThread("RiggedModel::BindPalette");
    // Bound by paletteNames_.size(), not palette.size(): the program declares a
    // fixed-size array, and uploading more matrices than the array holds is
    // INVALID_OPERATION (or silently truncates, depending on the driver). A caller
    // handing a shorter palette than the skeleton is a caller bug that deserves the
    // partial upload rather than leaving stale bones from the previous frame.
    const std::size_t count = std::min(paletteNames_.size(), palette.size());
    if (count > static_cast<std::size_t>(kMaxBonePalette)) {
        // Should be impossible (the loader refuses a bigger rig), but the GLSL array
        // is sized kMaxBonePalette, so writing past it is exactly the silent
        // truncation this guard exists for.
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        program.Set(paletteNames_[i], palette[i]);
    }
}

} // namespace gldx
