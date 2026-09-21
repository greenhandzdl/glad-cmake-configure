module;

#include "gldx/gmf.hpp"

#include <cstdio>

module gldx;

namespace gldx {

Framebuffer::~Framebuffer() { Delete(); }

void Framebuffer::Delete() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~Framebuffer");
        glDeleteFramebuffers(1, &id_);
        id_ = 0;
    }
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept : id_(other.id_) {
    other.id_ = 0;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        Delete();
        id_ = other.id_;
        other.id_ = 0;
    }
    return *this;
}

void Framebuffer::Create() {
    RenderContext::AssertRenderThread("Framebuffer::Create");
    if (id_ == 0) glGenFramebuffers(1, &id_);
}

void Framebuffer::AttachColor(const RenderTexture& tex, int layer, GLuint colorSlot) {
    RenderContext::AssertRenderThread("Framebuffer::AttachColor");
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    if (tex.target() == GL_TEXTURE_2D_ARRAY) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colorSlot,
                                  tex.id(), 0, layer);
    } else {
        // Plain 2D (and any non-array target): glFramebufferTextureLayer would
        // be an error here and leave the slot empty (=> INCOMPLETE_DRAW_BUFFER).
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colorSlot,
                               GL_TEXTURE_2D, tex.id(), 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::AttachDepth(const RenderTexture& tex, int layer) {
    RenderContext::AssertRenderThread("Framebuffer::AttachDepth");
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    if (tex.target() == GL_TEXTURE_2D_ARRAY) {
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tex.id(), 0, layer);
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex.id(), 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::AttachCubeFaceColor(const TextureCubeMap& cube, GLenum face, int level, GLuint colorSlot) {
    RenderContext::AssertRenderThread("Framebuffer::AttachCubeFaceColor");
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colorSlot, face, cube.id(), level);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::AttachCubeFaceDepth(const TextureCubeMap& cube, GLenum face, int level) {
    RenderContext::AssertRenderThread("Framebuffer::AttachCubeFaceDepth");
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, face, cube.id(), level);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::AttachColorMultisample(const RenderTexture& tex, GLuint colorSlot) {
    RenderContext::AssertRenderThread("Framebuffer::AttachColorMultisample");
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + colorSlot,
                           GL_TEXTURE_2D_MULTISAMPLE, tex.id(), 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::AttachDepthMultisample(const RenderTexture& tex) {
    RenderContext::AssertRenderThread("Framebuffer::AttachDepthMultisample");
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D_MULTISAMPLE, tex.id(), 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::ResolveColorTo(const Framebuffer& src, const Framebuffer& dst,
                                 int width, int height) {
    RenderContext::AssertRenderThread("Framebuffer::ResolveColorTo");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src.id_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst.id_);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::Bind() {
    RenderContext::AssertRenderThread("Framebuffer::Bind");
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
}

void Framebuffer::BindRead() const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, id_);
}

void Framebuffer::Unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::SetDrawBuffers(GLuint count, const GLenum* buffers) {
    RenderContext::AssertRenderThread("Framebuffer::SetDrawBuffers");
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    glDrawBuffers(count, buffers);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool Framebuffer::CheckComplete() const {
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[Framebuffer] incomplete: 0x%x\n", static_cast<unsigned>(status));
        return false;
    }
    return true;
}

void Framebuffer::Viewport(int width, int height) const {
    glViewport(0, 0, width, height);
}

void Framebuffer::ClearColor(const glm::vec4& c) const {
    glClearColor(c.r, c.g, c.b, c.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Framebuffer::ClearDepth(float d) const {
    glClearDepth(d);
    glClear(GL_DEPTH_BUFFER_BIT);
}

} // namespace gldx
