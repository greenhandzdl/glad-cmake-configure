// texture_samplers demo — the two-sampler render pass (see SamplerPass.h).
#include "gldx/core/Platform.h"   // <glad/gl.h>: GL_* filter/draw constants

#include <cstdint>
#include <cstdio>
#include <span>

#include <glm/glm.hpp>

// The engine as a single C++20 named module. Its global module fragment already
// attaches <span> (std::dynamic_extent) and the GLM headers (glm::qualifier) to
// the global module, so — as in every other demo TU — all std/GLM textual
// includes must precede the import; re-including them after it trips MSVC's
// global-module redefinition checks.
import gldx;

#include "SamplerPass.h"

namespace texture_samplers {
namespace {

constexpr const char* kVertex = R"GLSL(#version 410 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kFragment = R"GLSL(#version 410 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;
void main() { FragColor = texture(uTex, vUV); }
)GLSL";

// A colourful 32x32 checker with diagonal bands: unmistakably blocky under
// NEAREST, unmistakably soft under LINEAR once magnified.
gldx::Texture2DDesc MakeCheckerDesc(int size = 32) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 3;
    d.srgb = true;
    d.pixels.resize(static_cast<std::size_t>(size) * size * 3);
    const int cells = 8;
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool on = ((x / cell) + (y / cell)) % 2 == 0;
            const glm::vec3 a = on ? glm::vec3(0.95f, 0.35f, 0.25f) : glm::vec3(0.2f, 0.4f, 0.9f);
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            d.pixels[i + 0] = static_cast<std::uint8_t>(a.r * 255.0f);
            d.pixels[i + 1] = static_cast<std::uint8_t>(a.g * 255.0f);
            d.pixels[i + 2] = static_cast<std::uint8_t>(a.b * 255.0f);
        }
    }
    return d;
}

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc", "/System/Library/Fonts/Helvetica.ttc",
    "C:/Windows/Fonts/consola.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
};
gldx::Texture2DDesc MakeSolidDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1; d.channels = 4; d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

// 8 verts interleaved [x, y, u, v]: left quad then right quad, indexed as two
// triangles each. Left samples with the NEAREST sampler, right with LINEAR.
const GLfloat kVerts[] = {
    // left half            uv
    -0.95f, -0.55f, 0.0f, 0.0f,
    -0.05f, -0.55f, 1.0f, 0.0f,
    -0.05f,  0.55f, 1.0f, 1.0f,
    -0.95f,  0.55f, 0.0f, 1.0f,
    // right half
     0.05f, -0.55f, 0.0f, 0.0f,
     0.95f, -0.55f, 1.0f, 0.0f,
     0.95f,  0.55f, 1.0f, 1.0f,
     0.05f,  0.55f, 0.0f, 1.0f,
};
const GLuint kIdx[] = {0, 1, 2, 2, 1, 3,   4, 5, 6, 6, 5, 7};

} // namespace

SamplerPass::SamplerPass() : RenderPass("Samplers") {
    auto p = gldx::ShaderProgram::CreateFromSource(kVertex, kFragment);
    if (!p) { std::fprintf(stderr, "tex shader: %s\n", p.error().c_str()); return; }
    program_ = std::make_unique<gldx::ShaderProgram>(std::move(*p));

    tex_.Upload(MakeCheckerDesc());

    gldx::Sampler::Desc near_;
    near_.minFilter = near_.magFilter = GL_NEAREST;
    nearest_.Create(near_);
    gldx::Sampler::Desc lin_;
    lin_.minFilter = lin_.magFilter = GL_LINEAR;
    linear_.Create(lin_);

    // VertexArray / GLBuffer RAII (the engine's own Mesh composes these same
    // primitives) instead of hand-rolled glGen/glBind/glBufferData. Create
    // uploads then self-unbinds, so the bindings the VAO must record are made
    // explicitly inside the VAO scope below.
    vao_.Create();
    vbo_.Create(GL_ARRAY_BUFFER, std::span<const GLfloat>(kVerts));
    ebo_.Create(GL_ELEMENT_ARRAY_BUFFER, std::span<const GLuint>(kIdx));
    constexpr GLsizei stride = 4 * sizeof(GLfloat);
    vao_.Bind();
    vbo_.Bind(GL_ARRAY_BUFFER);
    vao_.AttachAttribute(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    vao_.AttachAttribute(1, 2, GL_FLOAT, GL_FALSE, stride,
                         reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
    ebo_.Bind(GL_ELEMENT_ARRAY_BUFFER);   // captured by the VAO on unbind
    vao_.Unbind();

    if (!sprite_.Init()) std::fprintf(stderr, "SpriteBatch init failed\n");
    for (const char* c : kFontCandidates) { if (font_.LoadFromFile(c, 48.0f)) break; }
    white_.Upload(MakeSolidDesc());
}

SamplerPass::~SamplerPass() {
    gldx::Sampler::Unbind(0);   // don't leave our sampler object bound past teardown
}

void SamplerPass::Execute(gldx::RenderFrame& f) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, f.fbWidth, f.fbHeight);
    glClearColor(0.10f, 0.11f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!program_ || !tex_.valid()) return;

    program_->Use();
    program_->Set("uTex", 0);
    tex_.Bind(0);

    vao_.Bind();
    nearest_.Bind(0);   // sampler object on unit 0
    vao_.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, reinterpret_cast<const void*>(0));
    linear_.Bind(0);
    vao_.DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT,
                      reinterpret_cast<const void*>(6 * sizeof(GLuint)));
    vao_.Unbind();
    gldx::Sampler::Unbind(0);

    if (font_.loaded() && white_.valid()) {
        sprite_.Begin(white_, f.fbWidth, f.fbHeight);
        gldx::TextRenderer::Draw(sprite_, font_, "NEAREST",
                                labelX(f, 0.25f), 0.62f * static_cast<float>(f.fbHeight),
                                24.0f, glm::vec4(1.0f));
        gldx::TextRenderer::Draw(sprite_, font_, "LINEAR",
                                labelX(f, 0.75f), 0.62f * static_cast<float>(f.fbHeight),
                                24.0f, glm::vec4(1.0f));
        sprite_.End();
    }
}

float SamplerPass::labelX(const gldx::RenderFrame& f, float frac) {
    return frac * static_cast<float>(f.fbWidth) - 46.0f;
}

} // namespace texture_samplers
