#ifndef GLDX_ASSETS_MODELLOADER_H
#define GLDX_ASSETS_MODELLOADER_H

/**
 * @file ModelLoader.h
 * @brief CPU-only 3D model import via Assimp (Stage A).
 *
 * Runs entirely off the render thread: it parses a file into MeshData value
 * types plus a list of texture file paths, with no GL calls. AssetManager
 * uploads the result on the render thread (Stage B).
 *
 * Assimp (and GLAD) are implementation details kept entirely in ModelLoader.cpp
 * so this public header - and therefore the `gldx` module interface that exports
 * it - never leaks assimp or GL types. The API speaks only gldx value types.
 */

#include <expected>
#include <string>
#include <vector>

#include "gldx/anim/Skeleton.h"
#include "gldx/anim/SkinnedVertex.h"
#include "gldx/geometry/Mesh.h"
#include "gldx/texture/Texture2D.h"

namespace gldx {

struct LoadedModelData {
    std::vector<MeshData>  meshes;
    std::vector<std::string> texturePaths;  // absolute paths of referenced color maps
    std::string            sourcePath;
    float                  scale = 1.0f;
};

// One texture belonging to a rigged model, already decoded on the CPU.
// `role` says what the file's material used it for, because the same PNG means
// two different things to a shader depending on it: an albedo map is sRGB-encoded
// colour, a normal map holds a vector, and a metallic/roughness map holds
// numbers. `desc.srgb` is set from that role, not from the file.
struct SkinnedTexture {
    enum class Role { Albedo, Normal, Metallic, Roughness };

    Role          role = Role::Albedo;
    std::string   name;                 // the reference as written in the file
    Texture2DDesc desc;                 // decoded pixels; empty when the bytes failed to decode
};

// The subset of a material this loader can hand to a shader. The floats are the
// file's scalar factors, which is what a shader should use when the matching map
// is absent; `*Texture` is an index into LoadedSkinnedModelData::textures, or -1.
struct SkinnedMaterial {
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    float     metallic  = 0.0f;
    float     roughness = 0.5f;
    bool      twoSided  = false;
    int       albedoTexture    = -1;
    int       normalTexture    = -1;
    int       metallicTexture  = -1;
    int       roughnessTexture = -1;
};

// A rigged model: skinned vertices plus the skeleton they are bound to.
// Skinning itself happens on the GPU (see SkinnedMesh + Skeleton), so this value
// type carries no per-frame pose - it is the bind-state input to that math, and
// like LoadedModelData it is safe to build on a worker thread.
struct LoadedSkinnedModelData {
    std::vector<SkinnedMeshData> meshes;
    Skeleton                     skeleton;       // parents-first; palette <= kMaxBonePalette
    // Materials, and one index per entry of `meshes` into `materials`.
    std::vector<SkinnedMaterial> materials;
    std::vector<int>             meshMaterial;
    // Textures the materials reference, in the order they were first used.
    std::vector<SkinnedTexture>  textures;
    // Referenced colour maps that could not be resolved to bytes (a path outside
    // the model's directory, or a file that is not there). Kept for diagnostics:
    // a caller that wants a log line should not have to re-parse the model.
    std::vector<std::string>     missingTexturePaths;
    std::string                  sourcePath;
};

class ModelLoader {
public:
    // Parse a model file. Thread-safe (no shared state, no GL).
    static std::expected<LoadedModelData, std::string>
    Load(const std::string& path);

    // Parse a rigged model: per-vertex bone indices/weights (4 influences,
    // normalised) plus the bone hierarchy. Thread-safe, no GL.
    //
    // Two things this path does that Load() must not:
    //   * It bakes each mesh node's scene transform into the vertices. Assimp
    //     keeps a mesh's vertices in that node's local space (a Z-up FBX, for
    //     instance, arrives with a -90 deg X rotation on the node), while the
    //     skinning matrices of the same file are expressed in bind/scene space;
    //     leaving the node transform unapplied would rotate the skeleton against
    //     its own mesh.
    //   * It derives the rest pose from the cluster bind matrices instead of the
    //     node hierarchy (see Skeleton.h for why that is the consistent choice).
    //
    // It also decodes the material's maps itself instead of returning paths: a
    // rigged FBX commonly *embeds* its textures (this repo's test asset carries
    // 29 MB of PNG inside the binary, keyed to a ".fbm/" folder that does not
    // exist on disk), and assimp copies those bytes into aiScene::mTextures. A
    // path-only contract would report a fully textured model as untextured.
    //
    // A file with no bones is not an error to report as a crash: it returns
    // std::unexpected, because the caller asked for a skin that is not there.
    static std::expected<LoadedSkinnedModelData, std::string>
    LoadSkinned(const std::string& path);

private:
    static std::string DirectoryOf(const std::string& path);
};

} // namespace gldx

#endif // GLDX_ASSETS_MODELLOADER_H
