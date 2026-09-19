#ifndef GFX_RENDER_RENDERPASSES_H
#define GFX_RENDER_RENDERPASSES_H

/**
 * @file RenderPasses.h
 * @brief The concrete frame stages, in execution order (plan "RenderPass").
 *
 *   ShadowPass      - depth-only render of casters into the cascade array
 *   GeometryPass    - linear HDR PBR scene (+ optional instanced field) + skybox
 *   PostProcessPass - MSAA resolve -> bloom -> ACES composite to the default FBO
 *   DebugHudPass    - world line overlay + sprite/text HUD, closes the profiler
 *
 * Each is a thin object whose Execute() contains the same GL sequence that used
 * to live inline in the application loop; nothing here owns GL resources.
 */

#include "gfx/render/RenderPass.h"

namespace gfx {

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

} // namespace gfx

#endif // GFX_RENDER_RENDERPASSES_H
