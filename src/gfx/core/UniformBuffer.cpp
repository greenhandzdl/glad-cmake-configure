module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

UniformBuffer::~UniformBuffer() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~UniformBuffer");
        glDeleteBuffers(1, &id_);
    }
}

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept
    : id_(other.id_), size_(other.size_) {
    other.id_ = 0;
    other.size_ = 0;
}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            RenderContext::AssertRenderThread("UniformBuffer::move=");
            glDeleteBuffers(1, &id_);
        }
        id_ = other.id_;
        size_ = other.size_;
        other.id_ = 0;
        other.size_ = 0;
    }
    return *this;
}

void UniformBuffer::Create(GLsizeiptr bytes, GLenum usage) {
    RenderContext::AssertRenderThread("UniformBuffer::Create");
    if (id_ == 0) glGenBuffers(1, &id_);
    glBindBuffer(GL_UNIFORM_BUFFER, id_);
    glBufferData(GL_UNIFORM_BUFFER, bytes, nullptr, usage);
    size_ = bytes;
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void UniformBuffer::Replace(std::span<const std::byte> data) {
    RenderContext::AssertRenderThread("UniformBuffer::Replace");
    if (id_ == 0) return;
    glBindBuffer(GL_UNIFORM_BUFFER, id_);
    if (static_cast<GLsizeiptr>(data.size_bytes()) != size_) {
        glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(data.size_bytes()),
                     data.data(), GL_DYNAMIC_DRAW);
        size_ = static_cast<GLsizeiptr>(data.size_bytes());
    } else if (!data.empty()) {
        glBufferSubData(GL_UNIFORM_BUFFER, 0, size_, data.data());
    }
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void UniformBuffer::BindBase(GLuint index) const {
    RenderContext::AssertRenderThread("UniformBuffer::BindBase");
    glBindBufferRange(GL_UNIFORM_BUFFER, index, id_, 0, size_);
}

void UniformBuffer::UnbindBase(GLuint index) {
    glBindBufferRange(GL_UNIFORM_BUFFER, index, 0, 0, 0);
}

} // namespace gfx
