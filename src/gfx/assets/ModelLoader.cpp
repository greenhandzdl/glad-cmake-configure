module;

#include "gfx/gmf.hpp"

// Assimp is an implementation detail of the assets module: <glad/gl.h> comes
// first so any transitive <GL/gl.h> pulled by Assimp is shadowed by our loader
// (project convention). These stay out of ModelLoader.h to keep them off the
// exported module interface.
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <cmath>
#include <cstdio>
#include <string_view>

module gfx;

namespace gfx {

namespace {
// True when a reference written inside a model file would land outside the
// directory the model itself lives in. assimp already confines the buffer uris a
// glTF points at to that directory ("../../x.bin" is refused with "could not
// open referenced file"), so the rule only has to be added where this file
// builds a path by hand - which it does for textures. An absolute reference was
// concatenated onto the directory too, which yields "/models//etc/hosts": not
// the file the model named, and not a readable path either, so refusing it here
// costs no case that used to work.
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

} // namespace gfx
