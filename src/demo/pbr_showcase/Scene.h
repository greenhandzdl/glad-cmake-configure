#ifndef PBR_SHOWCASE_SCENE_H
#define PBR_SHOWCASE_SCENE_H

/**
 * @file Scene.h
 * @brief The pbr_showcase scene graph + the CPU texture generators it feeds.
 *
 * Owns every mesh / material / the checker albedo so the scene-node addresses
 * stay stable for the whole run, and builds the actual content (ground, the
 * metallic x roughness sphere grid, checker cubes, the carousel pivot) plus the
 * GPU-instanced field. main.cpp stays the wiring; this is the "what's in the
 * world" half of the demo.
 *
 * NOTE: a demo header cannot itself `import gldx`, so this file names gldx
 * engine types and must be included AFTER `import gldx;` in each .cpp (the same
 * ordering main.cpp already uses for Platform.h + the module imports).
 */

#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace pbr_showcase {

// Owns the GPU mesh + CPU material for one scene object. Scene nodes point here;
// storing them by unique_ptr keeps the addresses stable across the whole run.
struct Owned {
    gldx::Mesh        mesh;
    gldx::PbrMaterial material;
};

// CPU-side checker albedo (Stage A, no GL).
gldx::Texture2DDesc MakeCheckerDesc(int size = 512, int cells = 8);

// 1x1 opaque white texel: a cheap solid fill for HUD panels via SpriteBatch.
gldx::Texture2DDesc MakeSolidDesc();

class ShowcaseScene {
public:
    // Uploads meshes + builds the node graph. Render-thread only (touches GL).
    void Build();

    [[nodiscard]] gldx::Scene&     scene()    noexcept { return scene_; }
    [[nodiscard]] gldx::SceneNode* carousel() noexcept { return carousel_; }
    [[nodiscard]] gldx::Texture2D& checker()  noexcept { return checker_; }

private:
    gldx::SceneNode& AddObject(gldx::MeshData data, const gldx::PbrMaterial& matCfg,
                               const gldx::Transform& xf, gldx::SceneNode* parent = nullptr);

    std::vector<std::unique_ptr<Owned>> owned_;
    gldx::Scene     scene_;
    gldx::Texture2D checker_;
    gldx::SceneNode* carousel_ = nullptr;
    int              nextId_   = 0;
};

// The GPU-instanced field (a grid of coloured boxes out toward +x), built into
// `out` (CPU data staged, uploaded once). Render-thread only.
void BuildInstancedField(gldx::InstancedMesh& out);

} // namespace pbr_showcase

#endif // PBR_SHOWCASE_SCENE_H
