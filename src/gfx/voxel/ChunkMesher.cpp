module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

namespace {

struct FaceDef {
    glm::ivec3 normal;
    glm::ivec3 uAxis;
    glm::ivec3 vAxis;   // cross(uAxis, vAxis) == normal  (CCW seen from outside)
};

// The six block faces. Corner order (00,10,11,01) walks the quad CCW when
// viewed from outside the block, which keeps back-face culling valid.
// Cross products checked by hand: +X: (0,0,-1)x(0,1,0)=(1,0,0); -X: (0,0,1)x(0,1,0)=(-1,0,0);
// +Y: (1,0,0)x(0,0,-1)=(0,1,0); -Y: (1,0,0)x(0,0,1)=(0,-1,0); +Z: (1,0,0)x(0,1,0)=(0,0,1);
// -Z: (-1,0,0)x(0,1,0)=(0,0,-1).
constexpr FaceDef kFaces[6] = {
    { { 1, 0, 0}, { 0, 0,-1}, { 0, 1, 0} },   // +X
    { {-1, 0, 0}, { 0, 0, 1}, { 0, 1, 0} },   // -X
    { { 0, 1, 0}, { 1, 0, 0}, { 0, 0,-1} },   // +Y (top)
    { { 0,-1, 0}, { 1, 0, 0}, { 0, 0, 1} },   // -Y (bottom)
    { { 0, 0, 1}, { 1, 0, 0}, { 0, 1, 0} },   // +Z
    { { 0, 0,-1}, {-1, 0, 0}, { 0, 1, 0} },   // -Z
};

// Minecraft-style directional tint: open sky beats walls beats floors.
float FaceShade(const glm::ivec3& n) {
    if (n.y > 0) return 1.00f;
    if (n.y < 0) return 0.55f;
    if (n.x != 0) return 0.72f;
    return 0.84f;
}

// 4-level AO ramp (0 = fully shaded corner, 3 = open) as light multipliers.
constexpr float kAoLevels[4] = { 0.42f, 0.64f, 0.82f, 1.00f };

// Corner (du,dv) in [0..1]^2 of the quad, CCW.
constexpr glm::ivec2 kCorners[4] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };

} // namespace

VoxelChunkMesh ChunkMesher::Build(const std::uint16_t* chunkCells,
                                 glm::ivec3 chunkOrigin,
                                 const IVoxelSource& source) const {
    VoxelChunkMesh result;

    for (int y = 0; y < kChunkSize; ++y) {
        for (int z = 0; z < kChunkSize; ++z) {
            for (int x = 0; x < kChunkSize; ++x) {
                const std::uint16_t id = chunkCells[VoxelIndex(x, y, z)];
                if (id == 0) continue;
                const BlockDef& def = registry_.Get(id);
                VoxelMeshData& out =
                    def.transparent ? result.transparent : result.opaque;

                const glm::ivec3 cell{x, y, z};
                const glm::ivec3 world = chunkOrigin + cell;

                for (const FaceDef& face : kFaces) {
                    const glm::ivec3 n = face.normal;
                    const std::uint16_t neighbor = source.Sample(world + n);
                    // Interior faces: opaque neighbours hide us; identical
                    // ids hide each other (water body / leaf cluster skins).
                    if (neighbor != 0 &&
                        (registry_.IsOpaque(neighbor) || neighbor == id)) {
                        continue;
                    }

                    // Corner (0,0) of the quad - the *minimum* world corner of
                    // the face. The face normal contributes +1 on its own axis,
                    // and so does any tangent axis that steps backwards (the
                    // corners then grow from the far edge toward the near one):
                    // without that second term a top face lands on the column
                    // in front of its block and leaves its own column open.
                    const glm::vec3 base(
                        cell.x + (n.x > 0 ? 1 : 0)
                            + (face.uAxis.x < 0 || face.vAxis.x < 0 ? 1 : 0),
                        cell.y + (n.y > 0 ? 1 : 0)
                            + (face.uAxis.y < 0 || face.vAxis.y < 0 ? 1 : 0),
                        cell.z + (n.z > 0 ? 1 : 0)
                            + (face.uAxis.z < 0 || face.vAxis.z < 0 ? 1 : 0));

                    const float shade = FaceShade(n);
                    std::uint32_t ao[4] = {3, 3, 3, 3};
                    const std::uint32_t first = static_cast<std::uint32_t>(out.vertices.size());

                    for (int c = 0; c < 4; ++c) {
                        const glm::ivec2 cc = kCorners[c];
                        // ±1 step from the corner, away from the quad centre.
                        const glm::ivec2 su(cc.x == 0 ? -1 : 1, cc.y == 0 ? -1 : 1);
                        const glm::ivec3 side1  = world + n + su.x * face.uAxis;
                        const glm::ivec3 side2  = world + n + su.y * face.vAxis;
                        const glm::ivec3 corner = world + n + su.x * face.uAxis
                                                         + su.y * face.vAxis;
                        const int s1 = registry_.IsOpaque(source.Sample(side1)) ? 1 : 0;
                        const int s2 = registry_.IsOpaque(source.Sample(side2)) ? 1 : 0;
                        const int cn = registry_.IsOpaque(source.Sample(corner)) ? 1 : 0;
                        ao[c] = static_cast<std::uint32_t>(
                            (s1 && s2) ? 0 : 3 - (s1 + s2 + cn));

                        VoxelVertex v;
                        v.position = base
                            + static_cast<float>(cc.x) * glm::vec3(face.uAxis)
                            + static_cast<float>(cc.y) * glm::vec3(face.vAxis);
                        v.uv = glm::vec2(cc.x, cc.y);
                        v.layer = static_cast<float>(def.texLayer);
                        v.light = shade * kAoLevels[ao[c]];
                        out.vertices.push_back(v);
                    }

                    // Split-diagonal choice hides the AO interpolation seam:
                    // the shared edge connects the *darker* corner pair, so
                    // the artefact falls inside shadow instead of across sun.
                    if (ao[0] + ao[2] > ao[1] + ao[3]) {
                        out.indices.insert(out.indices.end(),
                                           {first + 1, first + 2, first + 3,
                                            first + 1, first + 3, first});
                    } else {
                        out.indices.insert(out.indices.end(),
                                           {first, first + 1, first + 2,
                                            first, first + 2, first + 3});
                    }
                }
            }
        }
    }
    return result;
}

} // namespace gfx
