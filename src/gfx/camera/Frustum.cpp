module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

namespace {
glm::vec4 Normalized(glm::vec4 p) {
    const float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (len > 1e-8f) p /= len;
    return p;
}
} // namespace

void Frustum::Extract(const glm::mat4& vp) {
    // Row j of a column-major matrix: element (col, row) -> vp[col][row].
    auto row = [&vp](int j) {
        return glm::vec4(vp[0][j], vp[1][j], vp[2][j], vp[3][j]);
    };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    planes_[0] = Normalized(r3 + r0);   // left
    planes_[1] = Normalized(r3 - r0);   // right
    planes_[2] = Normalized(r3 + r1);   // bottom
    planes_[3] = Normalized(r3 - r1);   // top
    planes_[4] = Normalized(r3 + r2);   // near
    planes_[5] = Normalized(r3 - r2);   // far
}

Frustum::Result Frustum::IntersectSphere(const glm::vec3& center, float radius) const {
    Result res = Result::Inside;
    for (const glm::vec4& p : planes_) {
        const float dist = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
        if (dist < -radius) return Result::Outside;
        if (dist < radius) res = Result::Intersect;
    }
    return res;
}

Frustum::Result Frustum::IntersectAABB(const glm::vec3& mn, const glm::vec3& mx) const {
    for (const glm::vec4& p : planes_) {
        // "Positive vertex": the corner furthest along the plane normal.
        const glm::vec3 pv(p.x >= 0 ? mx.x : mn.x,
                           p.y >= 0 ? mx.y : mn.y,
                           p.z >= 0 ? mx.z : mn.z);
        if (p.x * pv.x + p.y * pv.y + p.z * pv.z + p.w < 0.0f)
            return Result::Outside;
    }
    return Result::Inside;
}

} // namespace gfx
