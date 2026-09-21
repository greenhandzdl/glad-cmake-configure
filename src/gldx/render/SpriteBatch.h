#ifndef GLDX_RENDER_SPRITEBATCH_H
#define GLDX_RENDER_SPRITEBATCH_H

/**
 * @file SpriteBatch.h
 * @brief Immediate-mode 2D quad batcher for the HUD / sprite overlay
 *        (plan "SpriteBatch": one shader + one dynamic vertex buffer).
 *
 * Usage (render thread, after PostProcessChain::Composite so the default
 * framebuffer holds the tone-mapped image):
 *   batch.Begin(atlas, fbW, fbH);
 *   batch.Draw(atlas, x, y, w, h, u0, v0, u1, v1, color);   // any number
 *   batch.End();
 *
 * Quads are accumulated on the CPU and flushed to the GPU in batches (when the
 * buffer fills or the bound texture changes), so a run of sprites sharing one
 * atlas becomes a single draw call. Coordinates are in pixels with the origin at
 * the top-left; an orthographic projection maps them to clip space.
 */

#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "gldx/core/GLBuffer.h"
#include "gldx/core/VertexArray.h"
#include "gldx/shader/ShaderProgram.h"
#include "gldx/texture/Texture2D.h"

namespace gldx {

class SpriteBatch {
public:
    // 8 floats, tightly packed (offsets 0 / 8 / 16).
    struct Vertex {
        glm::vec2 pos;
        glm::vec2 uv;
        glm::vec4 color;
    };

    bool Init();   // compile shader + create VAO/VBO. Render-thread.

    void Begin(const Texture2D& tex, int fbWidth, int fbHeight);
    // Draw one textured, tinted quad. UV (u0,v0) is the top-left of the source
    // region, (u1,v1) the bottom-right.
    void Draw(const Texture2D& tex, float x, float y, float w, float h,
              float u0, float v0, float u1, float v1, const glm::vec4& color);
    // Convenience: full-texture quad.
    void Draw(const Texture2D& tex, float x, float y, float w, float h,
              const glm::vec4& color) {
        Draw(tex, x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f, color);
    }
    void End();

private:
    void Flush();   // upload pending vertices + one draw call

    static constexpr int kCapacityQuads = 2048;

    ShaderProgram shader_;
    VertexArray   vao_;
    GLBuffer      vbo_;
    std::vector<Vertex> verts_;
    GLuint  curTex_ = 0;
};

} // namespace gldx

#endif // GLDX_RENDER_SPRITEBATCH_H
