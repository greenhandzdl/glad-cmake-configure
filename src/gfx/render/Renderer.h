#ifndef GFX_RENDER_RENDERER_H
#define GFX_RENDER_RENDERER_H

/**
 * @file Renderer.h
 * @brief Per-frame state / clear helpers. All calls are render-thread only.
 *
 * Phase 1 keeps Renderer intentionally minimal: it owns global GL state and
 * the frame boundary (viewport + clear). Concrete draw calls happen through
 * Mesh::Draw and ShaderProgram::Set from the application layer.
 */

#include <glm/glm.hpp>

namespace gfx {

class Renderer {
public:
    void Init();                               // one-time global state (depth/cull)
    void BeginFrame(int width, int height);
    void EndFrame();                           // currently a no-op (swap happens in main loop)

    void SetClearColor(const glm::vec4& color) noexcept { clearColor_ = color; }

private:
    glm::vec4 clearColor_{0.10f, 0.10f, 0.12f, 1.0f};
};

} // namespace gfx

#endif // GFX_RENDER_RENDERER_H
