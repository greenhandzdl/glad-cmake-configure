module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

bool SpriteBatch::Init() {
    RenderContext::AssertRenderThread("SpriteBatch::Init");

    auto p = ShaderProgram::CreateFromSource(shaders::kSpriteVertex, shaders::kSpriteFragment);
    if (!p) return false;
    shader_ = std::move(*p);

    vao_.Create();
    vbo_.Reserve(GL_ARRAY_BUFFER,
                 static_cast<std::size_t>(kCapacityQuads) * 6 * sizeof(Vertex),
                 GL_DYNAMIC_DRAW);

    vao_.Bind();
    vbo_.Bind(GL_ARRAY_BUFFER);
    constexpr GLsizei stride = sizeof(Vertex);
    vao_.AttachAttribute(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    vao_.AttachAttribute(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(8));
    vao_.AttachAttribute(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));
    vbo_.Unbind(GL_ARRAY_BUFFER);
    vao_.Unbind();

    verts_.reserve(static_cast<std::size_t>(kCapacityQuads) * 6);
    return true;
}

void SpriteBatch::Begin(const Texture2D& tex, int fbWidth, int fbHeight) {
    RenderContext::AssertRenderThread("SpriteBatch::Begin");
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_.Use();
    const glm::mat4 proj = glm::ortho(0.0f, static_cast<float>(fbWidth),
                                      static_cast<float>(fbHeight), 0.0f, -1.0f, 1.0f);
    shader_.Set("uProj", proj);
    shader_.Set("uTex", 0);

    vao_.Bind();
    tex.Bind(0);
    curTex_ = tex.id();
    verts_.clear();
}

void SpriteBatch::Draw(const Texture2D& tex, float x, float y, float w, float h,
                       float u0, float v0, float u1, float v1, const glm::vec4& color) {
    if (verts_.size() + 6 > verts_.capacity()) Flush();
    if (tex.id() != curTex_) {
        Flush();
        tex.Bind(0);
        curTex_ = tex.id();
    }

    const float x1 = x + w, y1 = y + h;
    const Vertex tl{{x,  y }, {u0, v0}, color};
    const Vertex tr{{x1, y }, {u1, v0}, color};
    const Vertex br{{x1, y1}, {u1, v1}, color};
    const Vertex bl{{x,  y1}, {u0, v1}, color};
    verts_.insert(verts_.end(), {tl, tr, br, tl, br, bl});
}

void SpriteBatch::Flush() {
    if (verts_.empty()) return;
    vbo_.SubData(std::span<const Vertex>(verts_));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts_.size()));
    verts_.clear();
}

void SpriteBatch::End() {
    RenderContext::AssertRenderThread("SpriteBatch::End");
    Flush();
    vao_.Unbind();
    ShaderProgram::Unuse();
    glDisable(GL_BLEND);
}

} // namespace gfx
