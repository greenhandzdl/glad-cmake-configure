module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

VertexArray::~VertexArray() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~VertexArray");
        glDeleteVertexArrays(1, &id_);
    }
}

VertexArray::VertexArray(VertexArray&& other) noexcept : id_(other.id_) {
    other.id_ = 0;
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            RenderContext::AssertRenderThread("VertexArray::move=");
            glDeleteVertexArrays(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void VertexArray::Create() {
    RenderContext::AssertRenderThread("VertexArray::Create");
    if (id_ != 0) {
        glDeleteVertexArrays(1, &id_);
    }
    glGenVertexArrays(1, &id_);
}

void VertexArray::Bind() const {
    RenderContext::AssertRenderThread("VertexArray::Bind");
    glBindVertexArray(id_);
}

void VertexArray::Unbind() const {
    glBindVertexArray(0);
}

void VertexArray::AttachAttribute(GLuint index, GLint componentCount, GLenum type,
                                  GLboolean normalized, GLsizei stride, const void* offset) const {
    RenderContext::AssertRenderThread("VertexArray::AttachAttribute");
    glVertexAttribPointer(index, componentCount, type, normalized, stride, offset);
    glEnableVertexAttribArray(index);
}

} // namespace gldx
