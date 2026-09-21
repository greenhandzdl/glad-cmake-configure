#ifndef GLDX_RENDER_RENDERPASSES_H
#define GLDX_RENDER_RENDERPASSES_H

/**
 * @file RenderPasses.h
 * @brief The concrete frame stages, in execution order (plan "RenderPass").
 *
 *   ShadowPass      - depth-only render of casters into the cascade array
 *   GeometryPass    - linear HDR PBR scene (+ optional instanced field)
 *   SkyboxPass      - optional: HDR env cube fills uncovered pixels (LEQUAL depth)
 *   VoxelOpaquePass - linear HDR chunked voxel terrain (opens the scene target)
 *   VoxelTransparentPass - back-to-front water, then world particles
 *   PostProcessPass - MSAA resolve -> bloom -> ACES composite to the default FBO
 *                     (optional: with no post chain the 3D passes render to the window)
 *   DebugHudPass    - world line overlay + sprite/text HUD, closes the profiler
 *
 * Each is a thin object whose Execute() contains the same GL sequence that used
 * to live inline in the application loop; nothing here owns GL resources.
 *
 * The two voxel passes are the alternative to GeometryPass for a voxel demo:
 * chunk render records do not fit the Scene node graph (they are meshed data
 * streams, not transformed nodes), so each pass holds its own pipeline tuple —
 * the chunk list plus the texture array — while camera / frustum / lights
 * still come from the shared RenderFrame. VoxelOpaquePass opens the scene
 * target the same way GeometryPass does.
 */

#include <vector>

#include "gldx/render/RenderPass.h"
#include "gldx/texture/Texture2DArray.h"   // complete type for the atlas query
#include "gldx/voxel/VoxelMeshGpu.h"

namespace gldx {

class ShaderProgram;

class ShadowPass : public RenderPass {
public:
    ShadowPass() : RenderPass("Shadow") {}
    void Execute(RenderFrame& frame) override;
};

class GeometryPass : public RenderPass {
public:
    GeometryPass() : RenderPass("Geometry") {}
    void Execute(RenderFrame& frame) override;
};

// Standalone sky stage. The opaque geometry / voxel pass opens the scene target
// and leaves it bound; SkyboxPass draws the HDR environment cube with the
// LEQUAL-depth trick to fill the pixels nothing covered, then PostProcessPass
// closes the target. Keeping the sky its own pass means a pipeline that never
// adds it does not touch SkyboxRenderer / EnvironmentMap at all - the sky is no
// longer welded into the geometry pass's responsibilities.
class SkyboxPass : public RenderPass {
public:
    SkyboxPass() : RenderPass("Skybox") {}
    void Execute(RenderFrame& frame) override;
};

// Everything both voxel passes need, gathered once by the demo. Non-owning:
// the program and the texture array belong to the application's RAII set, and
// the chunk records live in the demo's chunk grid (indexed alongside the CPU
// chunks the meshing jobs produce).
struct VoxelPipeline {
    const ShaderProgram*   prog   = nullptr;   // kVoxelVertex/kVoxelFragment
    const Texture2DArray*  atlas  = nullptr;   // block slices, layer = texLayer
    std::vector<VoxelChunkGpu>* chunks = nullptr;
    float waterAlpha = 0.55f;   // uOverrideAlpha for the blended pass
    float leafCutoff = 0.45f;   // uAlphaCutoff for the cutout geometry
    float time = 0.0f;          // seconds; drives the water uv scroll
    // When true the opaque pass skips back-face culling so solid geometry draws
    // two-sided. Off by default (culling a closed mesher surface is a free
    // ~half-triangle win); on reveals a chunk interior when the camera is inside
    // or below terrain, where exposed bottom faces would otherwise be culled.
    bool doubleSided = false;

    [[nodiscard]] bool ready() const noexcept {
        return prog && atlas && atlas->valid() && chunks && !chunks->empty();
    }
};

// Base of the pair: holds the pipeline tuple + the scratch sort buffer.
class VoxelPassBase : public RenderPass {
public:
    void SetPipeline(VoxelPipeline& vx) noexcept { vx_ = &vx; }
    [[nodiscard]] VoxelPipeline* pipeline() const noexcept { return vx_; }

protected:
    using RenderPass::RenderPass;   // inherit the named-pass constructor

    VoxelPipeline* vx_ = nullptr;
    // Chunk indices sorted per frame (opaque: draw order irrelevant; the
    // transparent pass re-sorts it back-to-front). Kept as a member so a
    // steady-state frame allocates nothing.
    std::vector<int> order_;
};

class VoxelOpaquePass : public VoxelPassBase {
public:
    VoxelOpaquePass() : VoxelPassBase("VoxelOpaque") {}
    void Execute(RenderFrame& frame) override;
};

class VoxelTransparentPass : public VoxelPassBase {
public:
    VoxelTransparentPass() : VoxelPassBase("VoxelTransparent") {}
    void Execute(RenderFrame& frame) override;
};

class PostProcessPass : public RenderPass {
public:
    PostProcessPass() : RenderPass("PostProcess") {}
    void Execute(RenderFrame& frame) override;
};

class DebugHudPass : public RenderPass {
public:
    DebugHudPass() : RenderPass("DebugHud") {}
    void Execute(RenderFrame& frame) override;
};

} // namespace gldx

#endif // GLDX_RENDER_RENDERPASSES_H
