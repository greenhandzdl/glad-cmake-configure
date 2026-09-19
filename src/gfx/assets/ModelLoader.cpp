#include "gfx/assets/ModelLoader.h"

#include <glm/glm.hpp>

namespace gfx {

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

        MeshData data;
        data.vertices.resize(src->mNumVertices);
        for (unsigned int v = 0; v < src->mNumVertices; ++v) {
            const aiVector3D& p = src->mVertices[v];
            Vertex& dst = data.vertices[v];
            dst.position = glm::vec3(p.x, p.y, p.z);
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

        const int materialIndex = resolveDiffuse(scene->mMaterials[src->mMaterialIndex]);

        // Emit indices per face (already triangulated) as a single range.
        const std::uint32_t start = static_cast<std::uint32_t>(data.indices.size());
        for (unsigned int f = 0; f < src->mNumFaces; ++f) {
            const aiFace& face = src->mFaces[f];
            for (unsigned int k = 0; k < face.mNumIndices; ++k) {
                data.indices.push_back(face.mIndices[k]);
            }
        }
        GeometryRange range;
        range.indexOffset = start;
        range.indexCount = static_cast<std::uint32_t>(data.indices.size()) - start;
        range.materialIndex = materialIndex;
        data.ranges.push_back(range);

        out.meshes.push_back(std::move(data));
    }

    if (out.meshes.empty()) {
        return std::unexpected("Assimp: no meshes imported from " + path);
    }
    return out;
}

} // namespace gfx
