module;

#include "gldx/gmf.hpp"

// Assimp is an implementation detail of the assets module: <glad/gl.h> comes
// first so any transitive <GL/gl.h> pulled by Assimp is shadowed by our loader
// (project convention). These stay out of ModelLoader.h to keep them off the
// exported module interface.
#ifndef GLDX_NO_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>
// Embedded texture bytes are decoded here (Stage A, worker thread) so the
// exported header never has to name stb. STB_IMAGE_IMPLEMENTATION lives in
// third_party/stb_image_impl.cpp; this include is declaration-only.
#include <stb_image.h>
#endif

#include <cmath>
#include <cstdio>
#include <string_view>

module gldx;

namespace gldx {

#ifdef GLDX_NO_ASSIMP
// Model import is compiled out (GLDX_ENABLE_ASSIMP=OFF). The header API stays
// identical so the `gldx` module interface does not change; a Load() call reports
// clearly that this build carries no assimp importer, so an app does not mistake
// the empty result for a malformed file.
std::expected<LoadedModelData, std::string>
ModelLoader::Load(const std::string& path) {
    std::fprintf(stderr, "[ModelLoader] %s: model import is unavailable in a build "
                         "configured with GLDX_ENABLE_ASSIMP=OFF\n", path.c_str());
    return std::unexpected("gldx built without Assimp: model import unavailable");
}
#else

namespace {
// True when a reference written inside a model file would land outside the
// directory the model itself lives in. assimp already confines the buffer uris a
// glTF points at to that directory ("../../x.bin" is refused with "could not
// open referenced file"), so the rule only has to be added where this file
// builds a path by hand - which it does for textures. An absolute reference was
// concatenated onto the directory too, which yields "/models//etc/hosts": not
// the file the model named, and not a readable path either, so refusing it here
// costs no case that used to work.
// Assimp's aiMatrix4x4 is row-major with the translation in a4/b4/c4; glm's is
// column-major. Transposing on the way across is the whole conversion, and
// getting it wrong is invisible until a rig comes out rotated by 90 degrees.
glm::mat4 toGlm(const aiMatrix4x4& m) {
    glm::mat4 r;
    r[0][0] = m.a1; r[0][1] = m.b1; r[0][2] = m.c1; r[0][3] = m.d1;
    r[1][0] = m.a2; r[1][1] = m.b2; r[1][2] = m.c2; r[1][3] = m.d2;
    r[2][0] = m.a3; r[2][1] = m.b3; r[2][2] = m.c3; r[2][3] = m.d3;
    r[3][0] = m.a4; r[3][1] = m.b4; r[3][2] = m.c4; r[3][3] = m.d4;
    return r;
}

bool LeavesDirectory(std::string_view ref) {
    if (ref.empty()) return true;
    if (ref.front() == '/' || ref.front() == '\\') return true;
    if (ref.size() >= 2 && ref[1] == ':') return true;              // Windows drive letter
    for (std::size_t pos = 0; pos < ref.size(); ) {
        const auto end = ref.find_first_of("/\\", pos);
        const auto stop = end == std::string_view::npos ? ref.size() : end;
        if (ref.compare(pos, stop - pos, "..") == 0) return true;
        pos = stop + 1;
    }
    return false;
}
} // namespace

std::string ModelLoader::DirectoryOf(const std::string& path) {
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? std::string() : path.substr(0, pos + 1);
}

std::expected<LoadedModelData, std::string>
ModelLoader::Load(const std::string& path) {
    Assimp::Importer importer;
    const unsigned int flags =
        aiProcess_Triangulate |          // OpenGL wants triangles
        aiProcess_GenSmoothNormals |     // ensure normals exist
        aiProcess_CalcTangentSpace |     // tangents for normal mapping
        aiProcess_JoinIdenticalVertices |
        aiProcess_RemoveRedundantMaterials;

    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode) {
        return std::unexpected(std::string("Assimp: ") + importer.GetErrorString());
    }

    LoadedModelData out;
    out.sourcePath = path;
    const std::string dir = DirectoryOf(path);

    // Helper: add (or reuse) a diffuse texture path, returning its index or -1.
    auto resolveDiffuse = [&](aiMaterial* mat) -> int {
        if (!mat) return -1;
        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) != AI_SUCCESS) return -1;
        if (LeavesDirectory(texPath.C_Str())) {
            std::fprintf(stderr, "[ModelLoader] %s: texture '%s' names a path outside the "
                                 "model's own directory and is not loaded\n",
                         path.c_str(), texPath.C_Str());
            return -1;
        }
        std::string full = dir + texPath.C_Str();
        for (std::size_t i = 0; i < out.texturePaths.size(); ++i) {
            if (out.texturePaths[i] == full) return static_cast<int>(i);
        }
        out.texturePaths.push_back(std::move(full));
        return static_cast<int>(out.texturePaths.size()) - 1;
    };

    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* src = scene->mMeshes[mi];
        if (!src || src->mNumVertices == 0) continue;

        unsigned int nonFinite = 0;
        MeshData data;
        data.vertices.resize(src->mNumVertices);
        for (unsigned int v = 0; v < src->mNumVertices; ++v) {
            const aiVector3D& p = src->mVertices[v];
            Vertex& dst = data.vertices[v];
            dst.position = glm::vec3(p.x, p.y, p.z);
            // A NaN position is legal to upload and impossible to see: the
            // triangle just never rasterises, which reads as "the model is
            // missing" rather than "the model is broken". Say so once.
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) ++nonFinite;
            if (src->mNormals) {
                const aiVector3D& n = src->mNormals[v];
                dst.normal = glm::vec3(n.x, n.y, n.z);
            }
            if (src->mTextureCoords[0]) {
                const aiVector3D& t = src->mTextureCoords[0][v];
                dst.uv = glm::vec2(t.x, t.y);
            }
            if (src->mTangents) {
                const aiVector3D& tg = src->mTangents[v];
                dst.tangent = glm::vec3(tg.x, tg.y, tg.z);
            }
        }

        // Guard the material lookup: Assimp normally keeps mMaterialIndex in
        // range, but resolveDiffuse() already treats a null material as "no
        // texture", so bound it rather than trusting a malformed scene.
        aiMaterial* mat = (src->mMaterialIndex < scene->mNumMaterials)
                              ? scene->mMaterials[src->mMaterialIndex]
                              : nullptr;
        const int materialIndex = resolveDiffuse(mat);

        // Emit indices per face (already triangulated) as a single range.
        const std::uint32_t start = static_cast<std::uint32_t>(data.indices.size());
        for (unsigned int f = 0; f < src->mNumFaces; ++f) {
            const aiFace& face = src->mFaces[f];
            // An index that names a vertex this mesh never declared would go
            // straight into a GPU buffer. The OBJ importer refuses such a file
            // on its own, but the invariant belongs here rather than in whichever
            // importer ran, and it costs one compare per face. The whole face is
            // dropped, not the stray index: dropping single indices would
            // mis-align the triples that `ranges` below counts out.
            bool usable = face.mIndices != nullptr;
            for (unsigned int k = 0; usable && k < face.mNumIndices; ++k)
                usable = face.mIndices[k] < src->mNumVertices;
            if (!usable) continue;
            for (unsigned int k = 0; k < face.mNumIndices; ++k) {
                data.indices.push_back(face.mIndices[k]);
            }
        }
        GeometryRange range;
        range.indexOffset = start;
        range.indexCount = static_cast<std::uint32_t>(data.indices.size()) - start;
        range.materialIndex = materialIndex;
        data.ranges.push_back(range);

        if (nonFinite)
            std::fprintf(stderr, "[ModelLoader] %s mesh %u: %u/%u positions are not finite "
                                 "and will not draw\n", path.c_str(), mi, nonFinite,
                         src->mNumVertices);

        out.meshes.push_back(std::move(data));
    }

    if (out.meshes.empty()) {
        return std::unexpected("Assimp: no meshes imported from " + path);
    }
    return out;
}


// ---------------------------------------------------------------------------
// Skinned path
// ---------------------------------------------------------------------------
std::expected<LoadedSkinnedModelData, std::string>
ModelLoader::LoadSkinned(const std::string& path) {
    Assimp::Importer importer;
    // Deliberately no aiProcess_JoinIdenticalVertices: it can only be safe when
    // it also compares the bone weights it is about to merge, and the static
    // path's reason for using it (fewer vertices) does not apply to a rig whose
    // vertices are already unique per influence set.
    // aiProcess_LimitBoneWeights is what makes the four-slot vertex layout in
    // SkinnedVertex.h correct instead of merely likely: without it Assimp hands
    // back whatever the file contains, and a vertex with five influences would
    // otherwise be silently truncated when the slots run out.
    const unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                               aiProcess_CalcTangentSpace | aiProcess_RemoveRedundantMaterials |
                               aiProcess_LimitBoneWeights;
    const aiScene* scene = importer.ReadFile(path, flags);
    if (!scene || !scene->mRootNode) {
        return std::unexpected(std::string("Assimp: ") + importer.GetErrorString());
    }

    LoadedSkinnedModelData out;
    out.sourcePath = path;

    // --- 1. scene transform per mesh -------------------------------------
    // Vertices come out of Assimp in the local space of the node that owns the
    // mesh, while the cluster bind matrices are in bind (scene) space. Baking
    // the node transform is what puts the two in one space; skipping it is how a
    // rigged FBX ends up with its skeleton rotated 90 degrees against its own
    // skin (the FBX importer keeps the Y-up -> Z-up node rotation on the mesh).
    std::vector<glm::mat4> meshNodeGlobal(scene->mNumMeshes, glm::mat4(1.0f));
    std::vector<char> meshNodeSet(scene->mNumMeshes, 0);
    // name -> parent name, for the whole node tree. This is the fallback topology
    // source: assimp's FBX importer leaves aiBone::mNode null (measured on this
    // repo's test asset - all 23 bones), so a loader that only walks mNode gets a
    // flat skeleton. A flat skeleton draws the bind pose perfectly (with an empty
    // pose every skinning matrix is the identity either way) and then animates
    // like a puppet with cut strings: the thigh rotates, the shin and foot stay
    // where they were. Hence: hierarchy from the node tree, transforms from the
    // cluster bind matrices.
    std::unordered_map<std::string, std::string> nodeParentOf;
    {
        // Explicit stack rather than a lambda that calls itself: a rig can be
        // deep, and the node tree has no cycles, so a work list is both simpler
        // to read and impossible to overflow the C stack on.
        struct Entry { const aiNode* node; glm::mat4 parent; };
        std::vector<Entry> stack{{scene->mRootNode, glm::mat4(1.0f)}};
        while (!stack.empty()) {
            const Entry e = stack.back();
            stack.pop_back();
            const glm::mat4 world = e.parent * toGlm(e.node->mTransformation);
            for (unsigned int k = 0; k < e.node->mNumMeshes; ++k) {
                const unsigned int mi = e.node->mMeshes[k];
                if (mi >= scene->mNumMeshes) continue;
                if (meshNodeSet[mi]) continue;   // first owning node wins
                meshNodeGlobal[mi] = world;
                meshNodeSet[mi] = 1;
            }
            for (unsigned int k = 0; k < e.node->mNumChildren; ++k) {
                nodeParentOf.emplace(e.node->mChildren[k]->mName.C_Str(), e.node->mName.C_Str());
                stack.push_back({e.node->mChildren[k], world});
            }
        }
    }

    // --- 2. bones: names, parent topology, bind matrices -------------------
    // The bind globals come from the inverse-bind matrices, not from the node
    // transforms (see Skeleton.h). That is also why the parent link is taken
    // from the node hierarchy - the hierarchy is trustworthy for *topology*,
    // it is only the transforms that disagree with the clusters.
    std::unordered_map<std::string, glm::mat4>     inverseBindOf;
    std::unordered_map<std::string, const aiNode*> nodeOf;
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* src = scene->mMeshes[mi];
        if (!src) continue;
        for (unsigned int b = 0; b < src->mNumBones; ++b) {
            const aiBone* bone = src->mBones[b];
            if (!bone) continue;
            const std::string name = bone->mName.C_Str();
            inverseBindOf.emplace(name, toGlm(bone->mOffsetMatrix));
            nodeOf.emplace(name, bone->mNode);
        }
    }
    if (inverseBindOf.empty()) {
        return std::unexpected("Assimp: no skinning data in " + path +
                               " (load it with Load() instead of LoadSkinned())");
    }
    if (inverseBindOf.size() > static_cast<std::size_t>(kMaxBonePalette)) {
        char msg[256];
        std::snprintf(msg, sizeof(msg),
                      "skeleton has %zu bones, above the %d-bone palette the skinning "
                      "shader declares", inverseBindOf.size(), kMaxBonePalette);
        return std::unexpected(msg);
    }

    // Parents-first insertion: recurse up the node tree until an ancestor that is
    // itself a bone, so Armature / root / any other non-skinned intermediate node
    // does not become a fake bone.
    std::function<int(const std::string&)> ensureBone = [&](const std::string& name) -> int {
        const int existing = out.skeleton.Find(name);
        if (existing >= 0) return existing;
        // Two topology sources, in order of directness:
        //   1. aiBone::mNode's parent chain (what most importers give, and it is
        //      the only one that cannot be fooled by duplicate names);
        //   2. the node tree's name->parent map. Needed because the FBX importer
        //      hands back mNode == nullptr, and "no parent" there silently turns
        //      every bone into a root.
        // The search walks *past* non-bone ancestors (Armature, root, an empty),
        // so only real joints become parents.
        int parentIndex = -1;
        const auto nodeIt = nodeOf.find(name);
        if (nodeIt != nodeOf.end() && nodeIt->second) {
            for (const aiNode* p = nodeIt->second->mParent; p; p = p->mParent) {
                if (inverseBindOf.find(p->mName.C_Str()) != inverseBindOf.end()) {
                    parentIndex = ensureBone(p->mName.C_Str());
                    break;
                }
            }
        }
        if (parentIndex < 0) {
            const auto parentIt = nodeParentOf.find(name);
            std::string ancestor = parentIt == nodeParentOf.end() ? std::string() : parentIt->second;
            for (std::size_t hops = 0; hops < 64 && !ancestor.empty(); ++hops) {
                if (inverseBindOf.find(ancestor) != inverseBindOf.end()) {
                    parentIndex = ensureBone(ancestor);
                    break;
                }
                const auto up = nodeParentOf.find(ancestor);
                if (up == nodeParentOf.end()) break;
                ancestor = up->second;
            }
        }
        Bone bone;
        bone.name = name;
        bone.parent = parentIndex;
        bone.inverseBind = inverseBindOf.at(name);
        // restLocal = bindParent^-1 * bindSelf, with bind* = inverseBind^-1.
        const glm::mat4 bindSelf = glm::inverse(bone.inverseBind);
        if (parentIndex < 0) {
            bone.restLocal = bindSelf;
        } else {
            const glm::mat4 bindParent = glm::inverse(out.skeleton.bones()[static_cast<std::size_t>(parentIndex)].inverseBind);
            bone.restLocal = glm::inverse(bindParent) * bindSelf;
        }
        return out.skeleton.Add(std::move(bone));
    };
    for (const auto& [name, unused] : inverseBindOf) {
        (void)unused;
        ensureBone(name);
    }

    // --- 3. meshes ---------------------------------------------------------
    const std::string dir = DirectoryOf(path);

    // --- 2b. materials and their maps --------------------------------------
    // A rigged FBX usually embeds its maps: assimp copies the bytes into
    // aiScene::mTextures and leaves the material pointing at a ".fbm/" path that
    // may not exist on disk. Both cases are handled here, in the only order that
    // is correct - embedded bytes first, because a path that happens to exist
    // next to the model is a different (and older) texture than the one the file
    // was authored with.
    std::unordered_map<std::string, int> textureIndexByName;
    std::unordered_map<std::string, int> missingTextureIndex;
    std::unordered_map<unsigned int, int> materialIndexBySceneIndex;

    auto decodeEmbedded = [&](const aiTexture* tex) -> Texture2DDesc {
        Texture2DDesc desc;
        if (!tex || !tex->pcData) return desc;
        if (tex->mHeight == 0) {
            // Compressed (PNG/JPG) blob: mWidth is the byte count.
            int w = 0, h = 0, channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(tex->pcData), static_cast<int>(tex->mWidth),
                &w, &h, &channels, 4);
            if (!pixels) return desc;
            desc.width = w;
            desc.height = h;
            desc.channels = 4;
            desc.pixels.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
            stbi_image_free(pixels);
            return desc;
        }
        // Uncompressed: mWidth x mHeight ARGB texels, handed over as raw memory.
        desc.width = static_cast<int>(tex->mWidth);
        desc.height = static_cast<int>(tex->mHeight);
        desc.channels = 4;
        desc.pixels.resize(static_cast<std::size_t>(desc.width) * desc.height * 4);
        for (std::size_t i = 0; i < static_cast<std::size_t>(desc.width) * desc.height; ++i) {
            const aiTexel& t = tex->pcData[i];
            desc.pixels[i * 4 + 0] = t.r;
            desc.pixels[i * 4 + 1] = t.g;
            desc.pixels[i * 4 + 2] = t.b;
            desc.pixels[i * 4 + 3] = t.a;
        }
        return desc;
    };

    auto loadMap = [&](const aiMaterial* mat, aiTextureType type,
                       SkinnedTexture::Role role) -> int {
        if (!mat) return -1;
        aiString ref;
        if (mat->GetTexture(type, 0, &ref) != AI_SUCCESS) return -1;
        const std::string name = ref.C_Str();
        if (const auto seen = textureIndexByName.find(name); seen != textureIndexByName.end()) {
            return seen->second;
        }
        if (const auto gone = missingTextureIndex.find(name); gone != missingTextureIndex.end()) {
            return -1;
        }

        SkinnedTexture texture;
        texture.role = role;
        texture.name = name;
        texture.desc.srgb = (role == SkinnedTexture::Role::Albedo);

        if (const aiTexture* embedded = scene->GetEmbeddedTexture(name.c_str())) {
            texture.desc = decodeEmbedded(embedded);
            if (texture.desc.pixels.empty()) {
                std::fprintf(stderr, "[ModelLoader] %s: embedded texture '%s' failed to decode\n",
                             path.c_str(), name.c_str());
            }
        } else if (!LeavesDirectory(name)) {
            // Not embedded, so it has to be a file: same directory rule as Load().
            const std::string full = dir + name;
            int w = 0, h = 0, channels = 0;
            stbi_uc* pixels = stbi_load(full.c_str(), &w, &h, &channels, 4);
            if (pixels) {
                texture.desc.width = w;
                texture.desc.height = h;
                texture.desc.channels = 4;
                texture.desc.pixels.assign(pixels, pixels + static_cast<std::size_t>(w) * h * 4);
                stbi_image_free(pixels);
            }
        } else {
            std::fprintf(stderr, "[ModelLoader] %s: texture '%s' names a path outside the "
                                 "model's own directory and is not loaded\n",
                         path.c_str(), name.c_str());
        }

        if (texture.desc.pixels.empty()) {
            missingTextureIndex.emplace(name, -1);
            out.missingTexturePaths.push_back(name);
            return -1;
        }
        const int index = static_cast<int>(out.textures.size());
        out.textures.push_back(std::move(texture));
        textureIndexByName.emplace(name, index);
        return index;
    };

    auto materialFor = [&](unsigned int sceneMaterialIndex) -> int {
        if (const auto seen = materialIndexBySceneIndex.find(sceneMaterialIndex);
            seen != materialIndexBySceneIndex.end()) {
            return seen->second;
        }
        SkinnedMaterial material;
        const aiMaterial* src =
            sceneMaterialIndex < scene->mNumMaterials ? scene->mMaterials[sceneMaterialIndex]
                                                     : nullptr;
        if (src) {
            aiColor4D base(1.0f, 1.0f, 1.0f, 1.0f);
            if (src->Get(AI_MATKEY_BASE_COLOR, base) != AI_SUCCESS) {
                src->Get(AI_MATKEY_COLOR_DIFFUSE, base);
            }
            material.baseColor = glm::vec4(base.r, base.g, base.b, base.a);
            src->Get(AI_MATKEY_METALLIC_FACTOR, material.metallic);
            src->Get(AI_MATKEY_ROUGHNESS_FACTOR, material.roughness);
            int twoSided = 0;
            if (src->Get(AI_MATKEY_TWOSIDED, twoSided) == AI_SUCCESS) material.twoSided = twoSided != 0;
            material.albedoTexture =
                loadMap(src, aiTextureType_DIFFUSE, SkinnedTexture::Role::Albedo);
            material.normalTexture =
                loadMap(src, aiTextureType_NORMALS, SkinnedTexture::Role::Normal);
            material.metallicTexture =
                loadMap(src, aiTextureType_METALNESS, SkinnedTexture::Role::Metallic);
            // Blender writes roughness to SHININESS on export; a glTF writes
            // DIFFUSE_ROUGHNESS. Accept either, since the file format is not the
            // shader's problem.
            material.roughnessTexture =
                loadMap(src, aiTextureType_DIFFUSE_ROUGHNESS, SkinnedTexture::Role::Roughness);
            if (material.roughnessTexture < 0) {
                material.roughnessTexture =
                    loadMap(src, aiTextureType_SHININESS, SkinnedTexture::Role::Roughness);
            }
        }
        const int index = static_cast<int>(out.materials.size());
        out.materials.push_back(material);
        materialIndexBySceneIndex.emplace(sceneMaterialIndex, index);
        return index;
    };

    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* src = scene->mMeshes[mi];
        if (!src || src->mNumVertices == 0) continue;
        if (src->mNumBones == 0) {
            std::fprintf(stderr, "[ModelLoader] %s mesh %u has no bones; skipped by the "
                                 "skinned path\n", path.c_str(), mi);
            continue;
        }

        const glm::mat4 nodeGlobal = meshNodeSet[mi] ? meshNodeGlobal[mi] : glm::mat4(1.0f);
        // Normals do not survive a non-uniform node scale through the position
        // matrix; the inverse-transpose keeps them perpendicular to the surface.
        const glm::mat3 normalMatrix = glm::mat3(glm::inverse(glm::transpose(glm::mat3(nodeGlobal))));

        SkinnedMeshData data;
        data.vertices.resize(src->mNumVertices);
        for (unsigned int v = 0; v < src->mNumVertices; ++v) {
            SkinnedVertex& dst = data.vertices[v];
            const aiVector3D& p = src->mVertices[v];
            const glm::vec4 world = nodeGlobal * glm::vec4(p.x, p.y, p.z, 1.0f);
            dst.position = glm::vec3(world);
            if (src->mNormals) {
                const aiVector3D& n = src->mNormals[v];
                dst.normal = normalMatrix * glm::vec3(n.x, n.y, n.z);
            }
            if (src->mTextureCoords[0]) {
                const aiVector3D& t = src->mTextureCoords[0][v];
                // v is flipped here, once, so that a consumer can sample a
                // decoded image directly. Assimp hands out UVs with v growing
                // upward from the bottom-left corner (the FBX/OBJ convention),
                // while a decoded PNG arrives top row first and OpenGL puts
                // v = 0 on that row - so without this the rig samples its atlas
                // mirrored. The symptom is not an obvious mirror: symmetric parts
                // (a black skirt disc, a boot) still look plausible and only the
                // asymmetric ones smear, which reads as "texture import is
                // broken" and sends you looking at the wrong end of the pipeline.
                // The static Load() path is deliberately left alone: its existing
                // demos sample their maps through their own shaders already.
                dst.uv = glm::vec2(t.x, 1.0f - t.y);
            }
        }

        // Scatter the (bone, weight) pairs into the four slots. A positive
        // weight marks a used slot, so the search for a free one is also the
        // "did this vertex already get four influences" test.
        unsigned int dropped = 0;
        for (unsigned int b = 0; b < src->mNumBones; ++b) {
            const aiBone* bone = src->mBones[b];
            if (!bone) continue;
            const int boneIndex = out.skeleton.Find(bone->mName.C_Str());
            if (boneIndex < 0) continue;
            for (unsigned int k = 0; k < bone->mNumWeights; ++k) {
                const aiVertexWeight& w = bone->mWeights[k];
                if (w.mVertexId >= src->mNumVertices) continue;
                SkinnedVertex& dst = data.vertices[w.mVertexId];
                if (!(w.mWeight > 0.0f)) continue;
                std::size_t slot = kMaxBoneInfluences;
                for (std::size_t s = 0; s < kMaxBoneInfluences; ++s) {
                    if (dst.boneWeight[s] <= 0.0f) { slot = s; break; }
                }
                if (slot == kMaxBoneInfluences) { ++dropped; continue; }
                dst.boneIndex[slot] = static_cast<unsigned int>(boneIndex);
                dst.boneWeight[slot] = w.mWeight;
            }
        }
        if (dropped) {
            std::fprintf(stderr, "[ModelLoader] %s mesh %u: dropped %u influences beyond the "
                                 "%zu-per-vertex limit\n", path.c_str(), mi, dropped,
                         kMaxBoneInfluences);
        }

        // Normalise, and give an unweighted vertex the root bone instead of
        // leaving a zero matrix behind (a zero palette row collapses the vertex
        // to the origin, which reads as a hole in the model, not as bad data).
        unsigned int unweighted = 0;
        for (SkinnedVertex& dst : data.vertices) {
            const float sum = dst.boneWeight.x + dst.boneWeight.y + dst.boneWeight.z + dst.boneWeight.w;
            if (sum > 0.0f) {
                dst.boneWeight /= sum;
            } else {
                ++unweighted;
                dst.boneIndex = glm::uvec4(0u);
                dst.boneWeight = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            }
        }
        if (unweighted) {
            std::fprintf(stderr, "[ModelLoader] %s mesh %u: %u vertices carry no bone weight; "
                                 "attached to bone 0\n", path.c_str(), mi, unweighted);
        }

        for (unsigned int f = 0; f < src->mNumFaces; ++f) {
            const aiFace& face = src->mFaces[f];
            if (!face.mIndices) continue;
            bool usable = true;
            for (unsigned int k = 0; k < face.mNumIndices; ++k) {
                usable = usable && face.mIndices[k] < src->mNumVertices;
            }
            if (!usable) continue;
            for (unsigned int k = 0; k < face.mNumIndices; ++k) {
                data.indices.push_back(face.mIndices[k]);
            }
        }
        if (data.indices.empty()) continue;

        out.meshMaterial.push_back(materialFor(src->mMaterialIndex));
        out.meshes.push_back(std::move(data));
    }

    if (out.meshes.empty()) {
        return std::unexpected("Assimp: no skinned meshes imported from " + path);
    }
    return out;
}

#endif // GLDX_NO_ASSIMP
} // namespace gldx
