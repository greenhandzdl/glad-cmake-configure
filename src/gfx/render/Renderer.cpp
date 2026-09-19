#include "gfx/render/Renderer.h"

#include <glad/gl.h>

#include "gfx/core/RenderContext.h"

namespace gfx {

void Renderer::Init() {
    RenderContext::AssertRenderThread("Renderer::Init");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}

void Renderer::BeginFrame(int width, int height) {
    RenderContext::AssertRenderThread("Renderer::BeginFrame");
    glViewport(0, 0, width, height);
    glClearColor(clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::EndFrame() {
    // Swap buffers happens in the main loop; hook kept for future passes.
}

} // namespace gfx
