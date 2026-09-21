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

    [[nodiscard]] GLuint id()    const noexcept { return id_; }
    [[nodiscard]] bool   valid() const noexcept { return id_ != 0; }

private:
    GLuint id_ = 0;
};

} // namespace gldx

#endif // GLDX_CORE_VERTEXARRAY_H
