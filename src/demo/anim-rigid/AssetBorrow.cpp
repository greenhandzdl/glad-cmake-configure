// Module first, then the headers that name its types.
import gldx;

#include "AssetBorrow.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#include <cstddef>
#include <utility>

namespace anim {

void BorrowMaterialMaps(const std::string& modelPath, gldx::LoadedSkinnedModelData& parsed) {
            // ---- a texture-less export borrows its maps from the original ----
            //
            // A Blender FBX export of an MMD model routinely drops every material map:
            // the .fbx and the .pmx here are the same 240-bone / 60-mesh / 800763-vertex
            // character, but the FBX has one empty material and `albedo no`, while the
            // PMX carries all 15 atlases. The meshes are in the SAME order with the SAME
            // vertex counts, so the material table can be transplanted mesh-for-mesh.
            //
            // This is a demo-side convenience, deliberately not a loader feature: the
            // loader's job is to report what the file contains, and "silently borrow a
            // sibling file's textures" is a policy this particular asset pair justifies
            // and a general one does not. It only fires when the parse came back with
            // NO albedo anywhere, so a self-contained asset is never touched.
            if (!parsed.meshes.empty()) {
                const bool anyMap = std::any_of(parsed.materials.begin(), parsed.materials.end(),
                                                [](const gldx::SkinnedMaterial& m) {
                                                    return m.albedoTexture >= 0;
                                                });
                if (!anyMap) {
                    // Candidates: first the same stem (`name.fbx` -> `name.pmx`), then
                    // every other `.pmx` in the same directory. The stem match alone is
                    // not enough here - this asset's FBX is called `未命名.fbx` while the
                    // PMX it was exported from is `Lamy_Idol_Mark2_1.0.pmx`, so the pair
                    // shares a directory but not a name. What actually identifies "same
                    // character" is the mesh sequence, and that is verified below before
                    // anything is taken.
                    std::vector<std::string> candidates;
                    const std::size_t slash = modelPath.find_last_of('/');
                    const std::string dir =
                        (slash == std::string::npos) ? std::string() : modelPath.substr(0, slash + 1);
                    const std::size_t dot = modelPath.find_last_of('.');
                    if (dot != std::string::npos && dot > slash) {
                        std::string stem = modelPath;
                        stem.replace(dot, std::string::npos, ".pmx");
                        if (stem != modelPath) candidates.push_back(stem);
                    }
                    std::error_code ec;
                    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                        if (!entry.is_regular_file()) continue;
                        const std::string p = entry.path().string();
                        if (p.size() > 4 && p.compare(p.size() - 4, 4, ".pmx") == 0 &&
                            p != modelPath && std::find(candidates.begin(), candidates.end(), p) == candidates.end()) {
                            candidates.push_back(p);
                        }
                    }
                    for (const std::string& sibling : candidates) {
                        auto donor = gldx::ModelLoader::LoadSkinned(sibling);
                        // The transplant is only honest when the two really are the same
                        // mesh sequence; a mismatch here would paint the face with the
                        // socks atlas, which is far worse than staying grey.
                        const bool sameShape =
                            donor && donor->meshes.size() == parsed.meshes.size() &&
                            std::equal(donor->meshes.begin(), donor->meshes.end(),
                                       parsed.meshes.begin(),
                                       [](const gldx::SkinnedMeshData& a,
                                          const gldx::SkinnedMeshData& b) {
                                           return a.vertices.size() == b.vertices.size();
                                       });
                        if (!sameShape) continue;
                        parsed.materials = std::move(donor->materials);
                        parsed.meshMaterial = std::move(donor->meshMaterial);
                        parsed.textures = std::move(donor->textures);
                        std::fprintf(stderr,
                                     "[anim-rigid] %s carries no material maps; borrowed "
                                     "%zu material(s) / %zu texture(s) from %s\n",
                                     modelPath.c_str(), parsed.materials.size(),
                                     parsed.textures.size(), sibling.c_str());
                        break;
                    }
                }
            }
}

}  // namespace anim
