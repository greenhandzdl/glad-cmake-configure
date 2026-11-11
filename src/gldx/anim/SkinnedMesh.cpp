module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

void SkinnedMesh::Upload(SkinnedMeshData&& data) {
    RenderContext::AssertRenderThread("SkinnedMesh::Upload");

    vertexCount_ = data.vertices.size();
    indexCount_ = data.indices.size();

    vbo_.Create(GL_ARRAY_BUFFER, std::span<const SkinnedVertex>(data.vertices));
    if (!data.indices.empty()) {
        ebo_.Create(GL_ELEMENT_ARRAY_BUFFER, std::span<const std::uint32_t>(data.indices));
    }

    vao_.Create();
    vao_.Bind();

    vbo_.Bind(GL_ARRAY_BUFFER);
    const auto stride = static_cast<GLsizei>(sizeof(SkinnedVertex));
    vao_.AttachAttribute(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, position)));
    vao_.AttachAttribute(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, normal)));
    vao_.AttachAttribute(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, uv)));
    // Bone indices go through glVertexAttribIPointer: they are indices, not
    // numbers to interpolate, and a float attribute would make the shader's
    // ivec4 conversion a rounding step away from reading the wrong bone.
    vao_.AttachIntegerAttribute(5, 4, GL_UNSIGNED_INT, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, boneIndex)));
    vao_.AttachAttribute(6, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(offsetof(SkinnedVertex, boneWeight)));

    if (ebo_.valid()) {
        ebo_.Bind(GL_ELEMENT_ARRAY_BUFFER);   // the VAO captures the element binding
    }

    vao_.Unbind();
    vbo_.Unbind(GL_ARRAY_BUFFER);
}

void SkinnedMesh::Draw() const {
    RenderContext::AssertRenderThread("SkinnedMesh::Draw");
    if (!vao_.valid()) return;
    vao_.Bind();
    if (ebo_.valid()) {
        vao_.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
    } else {
        vao_.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount_));
    }
    vao_.Unbind();
}

} // namespace gldx
