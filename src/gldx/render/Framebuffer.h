#ifndef GLDX_RENDER_FRAMEBUFFER_H
#define GLDX_RENDER_FRAMEBUFFER_H

/**
 * @file Framebuffer.h
 * @brief RAII, move-only OpenGL framebuffer object (offscreen render target).
 *
 * The plan's "Framebuffer/RenderTarget". Used by the shadow pass (attach one
 * layer of the cascade depth array), the IBL generators (attach a cube face or
 * a 2D target) and later the HDR/post chain. Attach helpers take ownership of
 * nothing — the caller keeps the RenderTexture/TextureCubeMap alive.
 * Render-thread only.
 */

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "gldx/texture/RenderTexture.h"
#include "gldx/texture/TextureCubeMap.h"

namespace gldx {

class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();

    Framebuffer(const Framebuffer&)            = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    void Create();   // glGenFramebuffers (render thread)

    // Attach a (layered) 2D texture as a colour slot or depth attachment.
    void AttachColor(const RenderTexture& tex, int layer = 0, GLuint colorSlot = 0);
    void AttachDepth(const RenderTexture& tex, int layer = 0);

    // Attach one face (and mip) of a cube map, used by IBL prefiltering.
    void AttachCubeFaceColor(const TextureCubeMap& cube, GLenum face, int level = 0, GLuint colorSlot = 0);
    void AttachCubeFaceDepth(const TextureCubeMap& cube, GLenum face, int level = 0);

    // Attach a GL_TEXTURE_2D_MULTISAMPLE texture (MSAA scene target). These use
    // glFramebufferTexture2D with the multisample target, not the layered path.
    void AttachColorMultisample(const RenderTexture& tex, GLuint colorSlot = 0);
    void AttachDepthMultisample(const RenderTexture& tex);

    // Blit-resolve src's colour attachment 0 (multisampled) into dst's colour
    // attachment 0 (single-sample). Both must already have their attachments.
    static void ResolveColorTo(const Framebuffer& src, const Framebuffer& dst,
                               int width, int height);

    void Bind();                    // glBindFramebuffer(GL_FRAMEBUFFER, id_)
    void BindRead() const;          // for blits
    void Unbind();
    void SetDrawBuffers(GLuint count, const GLenum* buffers);

    [[nodiscard]] bool CheckComplete() const;   // logs + returns status

    void Viewport(int width, int height) const; // glViewport(0,0,w,h)
    void ClearColor(const glm::vec4& c) const;
    void ClearDepth(float d = 1.0f) const;

    [[nodiscard]] GLuint id()    const noexcept { return id_; }
    [[nodiscard]] bool   valid() const noexcept { return id_ != 0; }

private:
    void Delete();
    GLuint id_ = 0;
};

} // namespace gldx

#endif // GLDX_RENDER_FRAMEBUFFER_H
