#ifndef GLDX_CORE_VERTEXARRAY_H
#define GLDX_CORE_VERTEXARRAY_H

/**
 * @file VertexArray.h
 * @brief RAII, move-only Vertex Array Object wrapper.
 *
 * Low-level owner of a VAO. Higher-level Mesh composes a VertexArray with
 * GLBuffer objects and an attribute layout. All calls are render-thread only.
 */

#include <glad/gl.h>

namespace gldx {

class TransformFeedback;   // DrawTransformFeedback() below reaches its id()

class VertexArray {
public:
    VertexArray() = default;
    ~VertexArray();

    VertexArray(const VertexArray&)            = delete;
    VertexArray& operator=(const VertexArray&) = delete;
    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;

    void Create();                 // glGenVertexArrays (render thread)
    void Bind() const;             // glBindVertexArray(id_)
    void Unbind() const;

    // Define one vertex attribute using the currently bound element state.
    // Must be called while this VAO is bound and `buffer` is bound to
    // GL_ARRAY_BUFFER.
    void AttachAttribute(GLuint index, GLint componentCount, GLenum type,
                         GLboolean normalized, GLsizei stride, const void* offset) const;

    // Draw against this VAO. These are the engine's only owners of the raw
    // glDraw* entry points: pass/demo code calls one of these instead of
    // reaching for glDrawArrays/glDrawElements directly, so the draw primitive
    // has a single source. They do NOT touch the binding—Bind()/Unbind() around
    // them stays the caller's concern. All render-thread only.
    void DrawArrays(GLenum mode, GLint first, GLsizei count) const;
    void DrawElements(GLenum mode, GLsizei count, GLenum type, const void* offset) const;
    void DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                               const void* offset, GLsizei instanceCount) const;

    // Draw as many primitives as a finished transform feedback capture session
    // generated, with no count argument: the number never reaches the CPU. This is
    // the read end of the pair that TransformFeedback is the write end of, and it is
    // what makes a GPU-decided workload size a workload size rather than a value the
    // host has to predict. `tf` must be a closed capture session, not the object
    // currently bound for one - its count is only final after End().
    void DrawTransformFeedback(GLenum mode, const TransformFeedback& tf) const;

    [[nodiscard]] GLuint id()    const noexcept { return id_; }
    [[nodiscard]] bool   valid() const noexcept { return id_ != 0; }

private:
    GLuint id_ = 0;
};

} // namespace gldx

#endif // GLDX_CORE_VERTEXARRAY_H
