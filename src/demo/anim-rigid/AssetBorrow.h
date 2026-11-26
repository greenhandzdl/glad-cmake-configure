// Borrow material maps from a sibling asset when the export dropped them.
//
// A Blender FBX export of an MMD model routinely drops every material map: the .fbx and
// the .pmx here are the same 240-bone / 60-mesh / 800763-vertex character, but the FBX
// ships one empty material and `albedo no`, while the PMX carries all 15 atlases.
//
// This is a demo-side convenience, deliberately not a loader feature: the loader must
// not go rummaging around its own directory for a different file format.
//
// NOTE: names gldx types, so include AFTER `import gldx;`.
#pragma once

#include <string>

namespace anim {

/**
 * If @p data has no material maps, look for a sibling .pmx/.fbx in the same directory
 * with the same mesh sequence and transplant its materials/textures/mesh-material table.
 *
 * The transplant is only honest when the two really are the same mesh sequence - a
 * mismatch would paint the face with the socks atlas, which is far worse than grey.
 */
void BorrowMaterialMaps(const std::string& modelPath, gldx::LoadedSkinnedModelData& data);

}  // namespace anim
