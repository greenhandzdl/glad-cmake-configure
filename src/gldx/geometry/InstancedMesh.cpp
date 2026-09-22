module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

bool InstancedMesh::Create(MeshData geometry, std::vector<Instance> instances) {
    RenderContext::AssertRenderThread("InstancedMesh::Create");
    if (geometry.vertices.empty() || geometry.indices.empty() || instances.empty())
        return false;

    indexCount_ = static_cast<std::uint32_t>(geometry.indices.size());
    instances_  = std::move(instances);
    count_      = static_cast<std::uint32_t>(instances_.size());

    geoVbo_.Create(GL_ARRAY_BUFFER, std::span<const Vertex>(geometry.vertices));
    ebo_.Create(GL_ELEMENT_ARRAY_BUFFER, std::span<const std::uint32_t>(geometry.indices));
    instVbo_.Create(GL_ARRAY_BUFFER, std::span<const Instance>(instances_), GL_DYNAMIC_DRAW);

    vao_.Create();
    vao_.Bind();

    // Geometry: positions (loc 0) + normals (loc 1), divisor 0 (per vertex).
    geoVbo_.Bind(GL_ARRAY_BUFFER);
    const GLsizei gs = static_cast<GLsizei>(sizeof(Vertex));
    vao_.AttachAttribute(0, 3, GL_FLOAT, GL_FALSE, gs,
                         reinterpret_cast<const void*>(VertexOffsets::position));
    vao_.AttachAttribute(1, 3, GL_FLOAT, GL_FALSE, gs,
                         reinterpret_cast<const void*>(VertexOffsets::normal));
    glVertexAttribDivisor(0, 0);
    glVertexAttribDivisor(1, 0);

    // Instance matrix across locations 4-7 (one vec4 column each) + colour at 8,
    // divisor 1 (advance once per instance).
    instVbo_.Bind(GL_ARRAY_BUFFER);
    const GLsizei is = static_cast<GLsizei>(sizeof(Instance));
    for (int c = 0; c < 4; ++c) {
        const auto* off = reinterpret_cast<const void*>(
            offsetof(Instance, model) + static_cast<std::size_t>(c) * sizeof(glm::vec4));
        vao_.AttachAttribute(4 + c, 4, GL_FLOAT, GL_FALSE, is, off);
        glVertexAttribDivisor(4 + c, 1);
    }
    vao_.AttachAttribute(8, 4, GL_FLOAT, GL_FALSE, is,
                         reinterpret_cast<const void*>(offsetof(Instance, color)));
    glVertexAttribDivisor(8, 1);

    ebo_.Bind(GL_ELEMENT_ARRAY_BUFFER);   // VAO captures the element binding

    geoVbo_.Unbind(GL_ARRAY_BUFFER);
    instVbo_.Unbind(GL_ARRAY_BUFFER);
    vao_.Unbind();
    return true;
}

void InstancedMesh::SetInstances(std::vector<Instance> instances) {
    RenderContext::AssertRenderThread("InstancedMesh::SetInstances");
    if (instances.empty() || !instVbo_.valid()) return;
    instances_ = std::move(instances);
    count_ = static_cast<std::uint32_t>(instances_.size());

    const std::size_t bytes = instances_.size() * sizeof(Instance);
    if (bytes == static_cast<std::size_t>(instVbo_.sizeBytes())) {
        instVbo_.SubData(std::span<const Instance>(instances_));   // same size: cheap
    } else {
        instVbo_.Replace(std::span<const Instance>(instances_));   // resizes, keeps id
    }
}

void InstancedMesh::Draw() const {
    RenderContext::AssertRenderThread("InstancedMesh::Draw");
    if (!vao_.valid() || count_ == 0) return;
    vao_.Bind();
    vao_.DrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount_),
                               GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(count_));
    vao_.Unbind();
}

} // namespace gldx
