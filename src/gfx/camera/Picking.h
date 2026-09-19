#ifndef GFX_CAMERA_PICKING_H
#define GFX_CAMERA_PICKING_H

/**
 * @file Picking.h
 * @brief CPU ray construction + ray/object hit tests for mouse picking (plan
 *        "Picking"). Header-only, pure math, read-only — naturally thread safe.
 *
 * The unproject uses the camera's inverse view-projection to turn a window pixel
 * into a world-space ray; objects are approximated by bounding spheres, which is
 * plenty for selecting scene entities from the HUD.
 */

#include <cfloat>
#include <cmath>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace gfx {

struct Ray {
    glm::vec3 origin;
    glm::vec3 dir;   // normalised
};

// pixelX from the left, pixelY from the top (GLFW cursor space).
inline Ray PickRay(float pixelX, float pixelY, int fbWidth, int fbHeight,
                   const glm::mat4& invViewProj, const glm::vec3& eye) {
    const float ndcX = 2.0f * pixelX / static_cast<float>(fbWidth)  - 1.0f;
    const float ndcY = 1.0f - 2.0f * pixelY / static_cast<float>(fbHeight);

    glm::vec4 farP = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    farP /= farP.w;
    glm::vec3 dir = glm::normalize(glm::vec3(farP) - eye);
    return {eye, dir};
}

// Nearest positive intersection distance with a sphere, or -1 if no hit.
inline float RaySphere(const Ray& r, const glm::vec3& center, float radius) {
    const glm::vec3 oc = center - r.origin;
    const float tca = glm::dot(oc, r.dir);
    const float d2 = glm::dot(oc, oc) - tca * tca;
    const float r2 = radius * radius;
    if (d2 > r2) return -1.0f;
    const float thc = std::sqrt(r2 - d2);
    float t0 = tca - thc;
    const float t1 = tca + thc;
    if (t0 < 0.0f) t0 = t1;         // origin inside the sphere
    if (t1 < 0.0f) return -1.0f;    // entirely behind the ray
    return t0 >= 0.0f ? t0 : -1.0f;
}

// Returns the index of the nearest sphere hit (front-to-back), or -1.
inline int PickNearest(const Ray& r,
                       const std::vector<std::pair<glm::vec3, float>>& spheres) {
    int best = -1;
    float bestT = FLT_MAX;
    for (std::size_t i = 0; i < spheres.size(); ++i) {
        const float t = RaySphere(r, spheres[i].first, spheres[i].second);
        if (t >= 0.0f && t < bestT) { bestT = t; best = static_cast<int>(i); }
    }
    return best;
}

} // namespace gfx

#endif // GFX_CAMERA_PICKING_H
