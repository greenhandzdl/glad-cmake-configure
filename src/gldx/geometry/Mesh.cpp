module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

void Mesh::Upload(MeshData&& data) {
    RenderContext::AssertRenderThread("Mesh::Upload");

    vertexCount_ = data.vertices.size();
    indexCount_  = data.indices.size();
    ranges_      = std::move(data.ranges);

    vbo_.Create(GL_ARRAY_BUFFER, std::span<const Vertex>(data.vertices));
    if (!data.indices.empty()) {
        ebo_.Create(GL_ELEMENT_ARRAY_BUFFER, std::span<const std::uint32_t>(data.indices));
    }

    vao_.Create();
    vao_.Bind();

    vbo_.Bind(GL_ARRAY_BUFFER);
    const auto stride = static_cast<GLsizei>(sizeof(Vertex));
    vao_.AttachAttribute(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(VertexOffsets::position));
    vao_.AttachAttribute(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(VertexOffsets::normal));
    vao_.AttachAttribute(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(VertexOffsets::uv));
    vao_.AttachAttribute(3, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(VertexOffsets::tangent));
    vao_.AttachAttribute(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(VertexOffsets::color));

    if (ebo_.valid()) {
        ebo_.Bind(GL_ELEMENT_ARRAY_BUFFER);  // VAO captures the element binding
    }

    vao_.Unbind();
    vbo_.Unbind(GL_ARRAY_BUFFER);
}

void Mesh::Update(MeshData&& data) {
    RenderContext::AssertRenderThread("Mesh::Update");
    if (!vao_.valid()) {           // first upload wins the full path
        Upload(std::move(data));
        return;
    }

    vertexCount_ = data.vertices.size();
    indexCount_  = data.indices.size();
    ranges_      = std::move(data.ranges);

    // glBufferData re-substitution orphans the old store, so sizes may change
    // freely and the GPU never stalls on an in-flight previous frame's copy.
    if (vbo_.valid()) {
        vbo_.Replace(std::span<const Vertex>(data.vertices));
    } else {
        vbo_.Create(GL_ARRAY_BUFFER, std::span<const Vertex>(data.vertices));
    }
    if (!data.indices.empty()) {
        if (ebo_.valid()) {
            ebo_.Replace(std::span<const std::uint32_t>(data.indices));
        } else {
            ebo_.Create(GL_ELEMENT_ARRAY_BUFFER, std::span<const std::uint32_t>(data.indices));
        }
    }

    // The VAO snapshots the element-buffer binding, so re-record it whenever an
    // EBO was (re)created after the VAO was built. Attribute bindings survive
    // because the VBO name never changes (Replace) — only its storage does.
    if (!data.indices.empty() && ebo_.valid()) {
        vao_.Bind();
        ebo_.Bind(GL_ELEMENT_ARRAY_BUFFER);
        vao_.Unbind();
    }
}

void Mesh::Draw() const {
    RenderContext::AssertRenderThread("Mesh::Draw");
    if (!vao_.valid()) return;
    vao_.Bind();
    if (!ranges_.empty()) {
        for (const auto& r : ranges_) DrawRange(r);
    } else if (ebo_.valid()) {
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexCount_));
    }
    vao_.Unbind();
}

void Mesh::DrawRange(const GeometryRange& range) const {
    RenderContext::AssertRenderThread("Mesh::DrawRange");
    if (!vao_.valid()) return;
    vao_.Bind();
    if (ebo_.valid()) {
        const auto* offset = reinterpret_cast<const void*>(
            static_cast<std::uintptr_t>(range.indexOffset) * sizeof(std::uint32_t));
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(range.indexCount), GL_UNSIGNED_INT, offset);
    } else {
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(range.indexOffset),
                     static_cast<GLsizei>(range.indexCount));
    }
    vao_.Unbind();
}

} // namespace gldx
