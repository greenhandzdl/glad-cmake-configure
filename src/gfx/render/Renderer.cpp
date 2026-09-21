module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

void Renderer::Init() {
    RenderContext::AssertRenderThread("Renderer::Init");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
}

void Renderer::AddPass(std::unique_ptr<RenderPass> pass) {
    passes_.push_back(std::move(pass));
}

void Renderer::BuildPbrPipeline() {
    passes_.clear();
    passes_.push_back(std::make_unique<ShadowPass>());
    passes_.push_back(std::make_unique<GeometryPass>());
    passes_.push_back(std::make_unique<SkyboxPass>());
    passes_.push_back(std::make_unique<PostProcessPass>());
    passes_.push_back(std::make_unique<DebugHudPass>());
}

void Renderer::BuildMinimalPipeline() {
    passes_.clear();
    passes_.push_back(std::make_unique<GeometryPass>());
    passes_.push_back(std::make_unique<DebugHudPass>());
}

void Renderer::Render(RenderFrame& frame) {
    RenderContext::AssertRenderThread("Renderer::Render");
    for (auto& pass : passes_)
        pass->Execute(frame);
}

} // namespace gfx
