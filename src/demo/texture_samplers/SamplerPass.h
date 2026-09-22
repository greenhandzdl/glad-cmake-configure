#ifndef TEXTURE_SAMPLERS_SAMPLER_PASS_H
#define TEXTURE_SAMPLERS_SAMPLER_PASS_H

/**
 * @file SamplerPass.h
 * @brief The one render pass the texture_samplers demo owns: a hand-built
 *        two-quad geometry + an inline sampler2D program, drawn twice against
 *        the same Texture2D with two different Sampler policies.
 *
 * Pulled out of main.cpp so the demo reads like the others: main wires the
 * window and renderer together, this pass owns the texture path being shown off.
 * The geometry goes through the gldx::VertexArray / gldx::GLBuffer RAII
 * wrappers rather than raw glGen/glBind calls, matching how the engine's own
 * Mesh composes those primitives.
 */

#include <memory>

import gldx;

namespace texture_samplers {

class SamplerPass : public gldx::RenderPass {
public:
    SamplerPass();
    ~SamplerPass() override;

    void Execute(gldx::RenderFrame& f) override;

private:
    static float labelX(const gldx::RenderFrame& f, float frac);

    std::unique_ptr<gldx::ShaderProgram> program_;
    gldx::Texture2D tex_;
    gldx::Sampler   nearest_, linear_;
    gldx::SpriteBatch sprite_;
    gldx::Font        font_;
    gldx::Texture2D   white_;
    // The two-quad VAO and its interleaved pos+uv vertex / index buffers, all
    // RAII + move-only; the pass holds them so they die with the GL context.
    gldx::VertexArray vao_;
    gldx::GLBuffer    vbo_, ebo_;
};

} // namespace texture_samplers

#endif // TEXTURE_SAMPLERS_SAMPLER_PASS_H
