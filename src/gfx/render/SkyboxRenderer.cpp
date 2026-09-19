module;

#include "gfx/gmf.hpp"

#include <cstdio>

module gfx;

namespace gfx {

bool SkyboxRenderer::Init() {
    RenderContext::AssertRenderThread("SkyboxRenderer::Init");
    auto prog = ShaderProgram::CreateFromSource(shaders::kSkyboxVertex, shaders::kSkyboxFragment);
    if (!prog) {
        std::fprintf(stderr, "[SkyboxRenderer] shader failed: %s\n", prog.error().c_str());
        return false;
    }
    shader_ = std::move(*prog);
    cube_.Upload(GeometryFactory::Cube(1.0f));
    ready_ = true;
    return true;
}

void SkyboxRenderer::Draw(const glm::mat4& viewProj, const TextureCubeMap& cube, unsigned unit) const {
    RenderContext::AssertRenderThread("SkyboxRenderer::Draw");
    if (!ready_ || !cube.valid()) return;

    shader_.Use();
    shader_.Set("uViewProj", viewProj);
    cube.Bind(unit);
    shader_.Set("uEnv", static_cast<int>(unit));

    // The skybox vertex shader writes gl_Position.z == w (depth 1.0); sampling
    // at LEQUAL draws it only where the depth buffer is still at the far clear.
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    cube_.Draw();
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
}

} // namespace gfx
