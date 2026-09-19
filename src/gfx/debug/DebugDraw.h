#ifndef GFX_DEBUG_DEBUGDRAW_H
#define GFX_DEBUG_DEBUGDRAW_H

/**
 * @file DebugDraw.h
 * @brief Immediate-mode coloured line overlay (plan "DebugDraw"): AABBs,
 *        coordinate axes, selection boxes, general helpers.
 *
 * Lines are accumulated on the CPU between Clear() and Draw() and rendered in a
 * single GL_LINES draw call. Owns one shader + one VAO + one streaming VBO of
 * fixed capacity; overflow is dropped (this is diagnostic geometry, not worth
 * re-allocating mid-frame). Callers push in world space; Draw applies the view
 * projection and disables the depth test so overlays read on top of the scene.
 */

#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "gfx/core/GLBuffer.h"
#include "gfx/core/VertexArray.h"
#include "gfx/shader/ShaderProgram.h"

namespace gfx {

class DebugDraw {
public:
    struct Vertex {
        glm::vec3 pos;
        glm::vec4 color;
    };

    bool Init();               // compile shader + create VAO/VBO (render thread)

    void Clear();              // drop last frame's lines; call before re-pushing
    void PushLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& c);
    void PushBox(const glm::vec3& mn, const glm::vec3& mx, const glm::vec4& c);
    void PushBoxCenter(const glm::vec3& center, const glm::vec3& half, const glm::vec4& c) {
        PushBox(center - half, center + half, c);
    }
    void PushAxes(const glm::vec3& origin, float length);

    // Bind target already set by caller (default framebuffer); applies view
    // projection, disables depth test, draws all queued lines. Render-thread.
    void Draw(const glm::mat4& viewProj);

    [[nodiscard]] std::size_t lineCount() const noexcept { return verts_.size() / 2; }

private:
    static constexpr std::size_t kCapacityVerts = 1u << 16;

    ShaderProgram shader_;
    VertexArray   vao_;
    GLBuffer      vbo_;
    std::vector<Vertex> verts_;
};

} // namespace gfx

#endif // GFX_DEBUG_DEBUGDRAW_H
