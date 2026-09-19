#include "gfx/geometry/GeometryFactory.h"

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace gfx {

MeshData GeometryFactory::Cube(float size) {
    const float h = size * 0.5f;
    // position, normal, uv  (per-face, 4 verts each)
    const glm::vec3 P[8] = {
        {-h,-h,-h},{ h,-h,-h},{ h, h,-h},{-h, h,-h},
        {-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h},
    };
    struct Face { int a,b,c,d; glm::vec3 n; };
    const Face faces[6] = {
        {0,3,2,1, { 0, 0,-1}},  // back  (-z)
        {4,5,6,7, { 0, 0, 1}},  // front (+z)
        {5,1,2,6, { 1, 0, 0}},  // right (+x)
        {0,4,7,3, {-1, 0, 0}},  // left  (-x)
        {3,7,6,2, { 0, 1, 0}},  // top   (+y)
        {0,1,5,4, { 0,-1, 0}},  // bottom(-y)
    };
    const glm::vec2 uv[4] = {{0,0},{1,0},{1,1},{0,1}};

    MeshData out;
    out.vertices.reserve(24);
    out.indices.reserve(36);
    for (const auto& f : faces) {
        const int q[4] = {f.a, f.b, f.c, f.d};
        const std::uint32_t base = static_cast<std::uint32_t>(out.vertices.size());
        for (int i = 0; i < 4; ++i) {
            Vertex v;
            v.position = P[q[i]];
            v.normal   = f.n;
            v.uv       = uv[i];
            out.vertices.push_back(v);
        }
        out.indices.insert(out.indices.end(), {base, base+1, base+2, base, base+2, base+3});
    }
    return out;
}

MeshData GeometryFactory::Quad(float width, float height) {
    const float hw = width * 0.5f, hh = height * 0.5f;
    MeshData out;
    out.vertices = {
        {{-hw,-hh,0},{0,0,1},{0,0}},
        {{ hw,-hh,0},{0,0,1},{1,0}},
        {{ hw, hh,0},{0,0,1},{1,1}},
        {{-hw, hh,0},{0,0,1},{0,1}},
    };
    out.indices = {0,1,2, 0,2,3};
    return out;
}

MeshData GeometryFactory::Sphere(float radius, int segments, int rings) {
    segments = segments < 3 ? 3 : segments;
    rings    = rings    < 2 ? 2 : rings;
    MeshData out;
    out.vertices.reserve((segments+1)*(rings+1));
    for (int r = 0; r <= rings; ++r) {
        const float phi = glm::pi<float>() * (static_cast<float>(r) / rings);       // 0..pi
        const float sp = std::sin(phi), cp = std::cos(phi);
        for (int s = 0; s <= segments; ++s) {
            const float theta = 2.0f * glm::pi<float>() * (static_cast<float>(s) / segments);
            glm::vec3 n(sp * std::cos(theta), cp, sp * std::sin(theta));
            Vertex v;
            v.position = n * radius;
            v.normal   = n;
            v.uv       = {static_cast<float>(s) / segments, static_cast<float>(r) / rings};
            out.vertices.push_back(v);
        }
    }
    const int stride = segments + 1;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const std::uint32_t i0 = r*stride + s;
            const std::uint32_t i1 = i0 + stride;
            out.indices.insert(out.indices.end(), {i0, i1, i0+1, i1, i1+1, i0+1});
        }
    }
    return out;
}

MeshData GeometryFactory::Plane(float halfExtent) {
    MeshData out;
    out.vertices = {
        {{-halfExtent,0,-halfExtent},{0,1,0},{0,0}},
        {{ halfExtent,0,-halfExtent},{0,1,0},{1,0}},
        {{ halfExtent,0, halfExtent},{0,1,0},{1,1}},
        {{-halfExtent,0, halfExtent},{0,1,0},{0,1}},
    };
    out.indices = {0,2,1, 0,3,2};  // CW so front face is +y under default cull setup
    return out;
}

} // namespace gfx
