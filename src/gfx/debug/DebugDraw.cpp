#include "gfx/debug/DebugDraw.h"

#include <cstddef>
#include <span>
#include <utility>

#include "gfx/core/RenderContext.h"
#include "gfx/shader/DebugShaders.h"

namespace gfx {

bool DebugDraw::Init() {
    RenderContext::AssertRenderThread("DebugDraw::Init");

    auto p = ShaderProgram::CreateFromSource(shaders::kDebugVertex, shaders::kDebugFragment);
    if (!p) return false;
    shader_ = std::move(*p);

    vao_.Create();
    vbo_.Reserve(GL_ARRAY_BUFFER, kCapacityVerts * sizeof(Vertex), GL_STREAM_DRAW);

    vao_.Bind();
    vbo_.Bind(GL_ARRAY_BUFFER);
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
    vao_.AttachAttribute(0, 3, GL_FLOAT, GL_FALSE, stride,
                         reinterpret_cast<const void*>(offsetof(Vertex, pos)));
    vao_.AttachAttribute(1, 4, GL_FLOAT, GL_FALSE, stride,
                         reinterpret_cast<const void*>(offsetof(Vertex, color)));
    vbo_.Unbind(GL_ARRAY_BUFFER);
    vao_.Unbind();

    verts_.reserve(4096);
    return true;
}

void DebugDraw::Clear() {
    verts_.clear();
}

void DebugDraw::PushLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& c) {
    if (verts_.size() + 2 > kCapacityVerts) return;
    verts_.push_back({a, c});
    verts_.push_back({b, c});
}

void DebugDraw::PushBox(const glm::vec3& mn, const glm::vec3& mx, const glm::vec4& c) {
    const glm::vec3 p[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
        {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
    };
    // bottom + top loops
    for (int i = 0; i < 4; ++i) { PushLine(p[i], p[(i + 1) % 4], c);
                                  PushLine(p[i + 4], p[((i + 1) % 4) + 4], c); }
    // verticals
    for (int i = 0; i < 4; ++i) PushLine(p[i], p[i + 4], c);
}

void DebugDraw::PushAxes(const glm::vec3& o, float len) {
    PushLine(o, o + glm::vec3(len, 0, 0), glm::vec4(1, 0, 0, 1));
    PushLine(o, o + glm::vec3(0, len, 0), glm::vec4(0, 1, 0, 1));
    PushLine(o, o + glm::vec3(0, 0, len), glm::vec4(0, 0, 1, 1));
}

void DebugDraw::Draw(const glm::mat4& viewProj) {
    RenderContext::AssertRenderThread("DebugDraw::Draw");
    if (verts_.empty()) return;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_.Use();
    shader_.Set("uViewProj", viewProj);
    vbo_.SubData(std::span<const Vertex>(verts_));
    vao_.Bind();
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts_.size()));
    vao_.Unbind();
    ShaderProgram::Unuse();

    glDisable(GL_BLEND);
}

} // namespace gfx
