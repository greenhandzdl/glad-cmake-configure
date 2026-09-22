module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

namespace {
// VoxelVertex attribute wiring (locations must match kVoxelVertex).
void AttachVoxelAttributes(VertexArray& vao, const GLBuffer& vbo) {
    constexpr GLsizei stride = static_cast<GLsizei>(sizeof(VoxelVertex));
    vbo.Bind(GL_ARRAY_BUFFER);
    vao.AttachAttribute(0, 3, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(VoxelVertex, position)));
    vao.AttachAttribute(1, 2, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(VoxelVertex, uv)));
    vao.AttachAttribute(2, 1, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(VoxelVertex, layer)));
    vao.AttachAttribute(3, 1, GL_FLOAT, GL_FALSE, stride,
                        reinterpret_cast<const void*>(offsetof(VoxelVertex, light)));
}
} // namespace

void VoxelMesh::Upload(VoxelMeshData&& data) {
    RenderContext::AssertRenderThread("VoxelMesh::Upload");

    vertexCount_ = data.vertices.size();
    indexCount_  = data.indices.size();

    vbo_.Create(GL_ARRAY_BUFFER, std::span<const VoxelVertex>(data.vertices));
    if (!data.indices.empty()) {
        ebo_.Create(GL_ELEMENT_ARRAY_BUFFER, std::span<const std::uint32_t>(data.indices));
    }

    vao_.Create();
    vao_.Bind();
    AttachVoxelAttributes(vao_, vbo_);
    if (ebo_.valid()) ebo_.Bind(GL_ELEMENT_ARRAY_BUFFER);
    vao_.Unbind();
    vbo_.Unbind(GL_ARRAY_BUFFER);
}

void VoxelMesh::Update(VoxelMeshData&& data) {
    RenderContext::AssertRenderThread("VoxelMesh::Update");
    if (!vao_.valid()) {
        Upload(std::move(data));
        return;
    }
    vertexCount_ = data.vertices.size();
    indexCount_  = data.indices.size();

    if (vbo_.valid()) {
        vbo_.Replace(std::span<const VoxelVertex>(data.vertices));
    } else {
        vbo_.Create(GL_ARRAY_BUFFER, std::span<const VoxelVertex>(data.vertices));
    }
    if (!data.indices.empty()) {
        if (ebo_.valid()) {
            ebo_.Replace(std::span<const std::uint32_t>(data.indices));
        } else {
            ebo_.Create(GL_ELEMENT_ARRAY_BUFFER, std::span<const std::uint32_t>(data.indices));
            // First EBO after the VAO existed: re-record the element binding.
            vao_.Bind();
            ebo_.Bind(GL_ELEMENT_ARRAY_BUFFER);
            vao_.Unbind();
        }
    }
}

void VoxelMesh::Draw() const {
    RenderContext::AssertRenderThread("VoxelMesh::Draw");
    if (!vao_.valid()) return;
    vao_.Bind();
    if (ebo_.valid() && indexCount_ > 0) {
        vao_.DrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_),
                          GL_UNSIGNED_INT, nullptr);
    }
    vao_.Unbind();
}

} // namespace gldx
