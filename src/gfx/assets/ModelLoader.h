#ifndef GFX_ASSETS_MODELLOADER_H
#define GFX_ASSETS_MODELLOADER_H

/**
 * @file ModelLoader.h
 * @brief CPU-only 3D model import via Assimp (Stage A).
 *
 * Runs entirely off the render thread: it parses a file into MeshData value
 * types plus a list of texture file paths, with no GL calls. AssetManager
 * uploads the result on the render thread (Stage B).
 *
 * Assimp (and GLAD) are implementation details kept entirely in ModelLoader.cpp
 * so this public header - and therefore the `gfx` module interface that exports
 * it - never leaks assimp or GL types. The API speaks only gfx value types.
 */

#include <expected>
#include <string>
#include <vector>

#include "gfx/geometry/Mesh.h"

namespace gfx {

struct LoadedModelData {
    std::vector<MeshData>  meshes;
    std::vector<std::string> texturePaths;  // absolute paths of referenced color maps
    std::string            sourcePath;
    float                  scale = 1.0f;
};

class ModelLoader {
public:
    // Parse a model file. Thread-safe (no shared state, no GL).
    static std::expected<LoadedModelData, std::string>
    Load(const std::string& path);

private:
    static std::string DirectoryOf(const std::string& path);
};

} // namespace gfx

#endif // GFX_ASSETS_MODELLOADER_H
