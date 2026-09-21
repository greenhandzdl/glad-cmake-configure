/**
 * @file main.cpp
 * @brief texture_samplers - one Texture2D, two sampler policies, side by side.
 *
 * Texture2D and Sampler are separate RAII objects. The texture carries pixels
 * (Upload from a CPU Texture2DDesc, Stage A -> B like every other resource);
 * the sampler carries the *policy* - min/mag filter, wrap, LOD clamp, optional
 * shadow compare. Because they are decoupled, the same texture can be sampled
 * two ways by binding a different sampler object to the same texture unit between
 * draws. That is exactly what this demo shows: a tiny 32x32 procedural checker,
 * magnified to fill each half of the window, sampled NEAREST on the left (hard
 * texel blocks) and LINEAR on the right (smooth interpolation).
 *
 * There is no lighting / PBR / scene here: a hand-built two-quad VAO (pos + uv)
 * and an inline sampler2D program, so nothing but the texture path is exercised.
 * The low source resolution is the point - at 32x32 blown up to hundreds of
 * pixels, nearest and linear are unmistakably different.
 *
 * Controls: Esc quits, --quit-after SECONDS for headless.
 */

#include "demo/demo_app.h"

#include <cstdint>
#include <cstdio>
#include <memory>

#include <glm/glm.hpp>

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
gfx::Texture2DDesc MakeCheckerDesc(int size = 32) {
    gfx::Texture2DDesc d;
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
gfx::Texture2DDesc MakeSolidDesc() {
    gfx::Texture2DDesc d;
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

class SamplerPass : public gfx::RenderPass {
public:
    SamplerPass() : RenderPass("Samplers") {
        auto p = gfx::ShaderProgram::CreateFromSource(kVertex, kFragment);
        if (!p) { std::fprintf(stderr, "tex shader: %s\n", p.error().c_str()); return; }
        program_ = std::make_unique<gfx::ShaderProgram>(std::move(*p));

        tex_.Upload(MakeCheckerDesc());

        gfx::Sampler::Desc near_;
        near_.minFilter = near_.magFilter = GL_NEAREST;
        nearest_.Create(near_);
        gfx::Sampler::Desc lin_;
        lin_.minFilter = lin_.magFilter = GL_LINEAR;
        linear_.Create(lin_);

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glGenBuffers(1, &ebo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kVerts), kVerts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIdx), kIdx, GL_STATIC_DRAW);
        constexpr GLsizei stride = 4 * sizeof(GLfloat);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(2 * sizeof(GLfloat)));
        glBindVertexArray(0);

        if (!sprite_.Init()) std::fprintf(stderr, "SpriteBatch init failed\n");
        for (const char* c : kFontCandidates) { if (font_.LoadFromFile(c, 48.0f)) break; }
        white_.Upload(MakeSolidDesc());
    }

    ~SamplerPass() override {
        if (vao_) glDeleteVertexArrays(1, &vao_);
        if (vbo_) glDeleteBuffers(1, &vbo_);
        if (ebo_) glDeleteBuffers(1, &ebo_);
        glBindSampler(0, 0);   // don't leave our sampler objects bound past teardown
    }

    void Execute(gfx::RenderFrame& f) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(0.10f, 0.11f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!program_ || !tex_.valid()) return;

        program_->Use();
        program_->Set("uTex", 0);
        tex_.Bind(0);

        glBindVertexArray(vao_);
        nearest_.Bind(0);   // sampler object on unit 0
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, reinterpret_cast<const void*>(0));
        linear_.Bind(0);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(6 * sizeof(GLuint)));
        glBindVertexArray(0);
        glBindSampler(0, 0);

        if (font_.loaded() && white_.valid()) {
            sprite_.Begin(white_, f.fbWidth, f.fbHeight);
            gfx::TextRenderer::Draw(sprite_, font_, "NEAREST",
                                    labelX(f, 0.25f), 0.62f * static_cast<float>(f.fbHeight),
                                    24.0f, glm::vec4(1.0f));
            gfx::TextRenderer::Draw(sprite_, font_, "LINEAR",
                                    labelX(f, 0.75f), 0.62f * static_cast<float>(f.fbHeight),
                                    24.0f, glm::vec4(1.0f));
            sprite_.End();
        }
    }

private:
    static float labelX(const gfx::RenderFrame& f, float frac) {
        return frac * static_cast<float>(f.fbWidth) - 46.0f;
    }

    std::unique_ptr<gfx::ShaderProgram> program_;
    gfx::Texture2D tex_;
    gfx::Sampler   nearest_, linear_;
    gfx::SpriteBatch sprite_;
    gfx::Font        font_;
    gfx::Texture2D   white_;
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
};

} // namespace

int main(int argc, char** argv) {
    const demo::Flags flags(argc, argv, {}, {}, "texture_samplers");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    return demo::Run(flags, "gfx demo - texture_samplers (Texture2D + Sampler)",
                     [&](demo::Ctx& ctx) -> int {
        ctx.renderer.AddPass(std::make_unique<SamplerPass>());
        return ctx.Loop([](gfx::RenderFrame&, const demo::FrameInfo&) {});
    });
}
