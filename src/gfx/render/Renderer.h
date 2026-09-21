#ifndef GFX_RENDER_RENDERER_H
#define GFX_RENDER_RENDERER_H

/**
 * @file Renderer.h
 * @brief Frame orchestrator: owns global GL state and an ordered list of render
 *        passes (plan "Renderer" / "RenderPass"). All calls are render-thread.
 *
 * The application builds a RenderFrame each frame and calls Render(), which runs
 * the passes in sequence. This replaces the old BeginFrame/EndFrame + inline draw
 * code: frame timing (glClear / targets) now lives inside the passes, so the
 * Renderer is purely about global state and stage ordering.
 */

#include <memory>
#include <vector>

#include "gfx/render/RenderPass.h"

namespace gfx {

struct RenderFrame;

class Renderer {
public:
    Renderer() = default;

    // One-time global state (depth test / face culling). Render-thread only.
    void Init();

    // Append a custom pass (advanced use). Render-thread only.
    void AddPass(std::unique_ptr<RenderPass> pass);

    // Compose the standard PBR showcase stage order:
    //   Shadow -> Geometry -> Skybox -> PostProcess -> DebugHud
    void BuildPbrPipeline();

    // Smallest useful stage order: Geometry -> DebugHud, with no shadow / sky /
    // post. The geometry pass then renders straight to the default framebuffer,
    // demonstrating that none of those subsystems is mandatory to draw a scene.
    void BuildMinimalPipeline();

    // Execute every pass in order against the shared frame. Render-thread only.
    void Render(RenderFrame& frame);

private:
    std::vector<std::unique_ptr<RenderPass>> passes_;
};

} // namespace gfx

#endif // GFX_RENDER_RENDERER_H
