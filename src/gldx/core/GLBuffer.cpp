module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

GLBuffer::~GLBuffer() {
    // Deletion is a GL call; only safe on the render thread. If a buffer is
    // ever destroyed off-thread the assert fires (AssetManager owns and frees
    // all GPU resources on the render thread by construction).
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~GLBuffer");
        glDeleteBuffers(1, &id_);
    }
}

GLBuffer::GLBuffer(GLBuffer&& other) noexcept
    : id_(other.id_), target_(other.target_), sizeBytes_(other.sizeBytes_) {
    other.id_ = 0;
    other.sizeBytes_ = 0;
}

GLBuffer& GLBuffer::operator=(GLBuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            RenderContext::AssertRenderThread("GLBuffer::move=");
            glDeleteBuffers(1, &id_);
        }
        id_        = other.id_;
        target_    = other.target_;
        sizeBytes_ = other.sizeBytes_;
        other.id_ = 0;
        other.sizeBytes_ = 0;
    }
    return *this;
}

void GLBuffer::Create(GLenum target, std::span<const std::byte> data, GLenum usage) {
    RenderContext::AssertRenderThread("GLBuffer::Create");
    if (id_ != 0) {
        glDeleteBuffers(1, &id_);
    }
    target_ = target;
    glGenBuffers(1, &id_);
    glBindBuffer(target_, id_);
    sizeBytes_ = static_cast<GLsizei>(data.size_bytes());
    glBufferData(target_, sizeBytes_, data.empty() ? nullptr : data.data(), usage);
    glBindBuffer(target_, 0);
}

void GLBuffer::Reserve(GLenum target, std::size_t byteCount, GLenum usage) {
    RenderContext::AssertRenderThread("GLBuffer::Reserve");
    if (id_ != 0) {
        glDeleteBuffers(1, &id_);
    }
    target_ = target;
    glGenBuffers(1, &id_);
    glBindBuffer(target_, id_);
    sizeBytes_ = static_cast<GLsizei>(byteCount);
    glBufferData(target_, sizeBytes_, nullptr, usage);
    glBindBuffer(target_, 0);
}

void GLBuffer::SubData(std::span<const std::byte> data, std::size_t offsetBytes) {
    RenderContext::AssertRenderThread("GLBuffer::SubData");
    if (id_ == 0 || data.empty()) return;
    glBindBuffer(target_, id_);
    glBufferSubData(target_, static_cast<GLintptr>(offsetBytes),
                    static_cast<GLsizeiptr>(data.size_bytes()), data.data());
    glBindBuffer(target_, 0);
}

void GLBuffer::Replace(std::span<const std::byte> data) {
    RenderContext::AssertRenderThread("GLBuffer::Replace");
    if (id_ == 0) return;
    glBindBuffer(target_, id_);
    sizeBytes_ = static_cast<GLsizei>(data.size_bytes());
    glBufferData(target_, sizeBytes_, data.empty() ? nullptr : data.data(), GL_STATIC_DRAW);
    glBindBuffer(target_, 0);
}

void GLBuffer::Bind(GLenum target) const {
    RenderContext::AssertRenderThread("GLBuffer::Bind");
    glBindBuffer(target, id_);
}

void GLBuffer::Unbind(GLenum target) const {
    glBindBuffer(target, 0);
}

} // namespace gldx
