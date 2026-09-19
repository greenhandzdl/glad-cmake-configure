#ifndef GFX_LIGHT_LIGHT_H
#define GFX_LIGHT_LIGHT_H

/**
 * @file Light.h
 * @brief CPU-side light data + a std140-compatible GPU block (no GL).
 *
 * LightSetup is the artist/game-facing description (one directional "sun" that
 * drives cascaded shadows + a bounded list of point lights). PackLighting()
 * lowers it into LightingBlockGpu, whose layout mirrors the GLSL
 * `layout(std140) uniform LightingBlock` consumed by the PBR shader. Because
 * every GPU-block member is a 16-byte vec4/ivec4, the C++ struct needs no
 * manual padding to match std140.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace gfx {

inline constexpr int kMaxPointLights = 32;

// ---- CPU-side (game) representation ----
struct DirectionalLight {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    glm::vec3 direction{0.0f, -1.0f, 0.0f};  // travel direction, normalized
};

struct PointLight {
    glm::vec3 position{0.0f};
    float range = 10.0f;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

struct LightSetup {
    DirectionalLight sun;
    std::vector<PointLight> points;          // capped to kMaxPointLights when packed
    glm::vec3 ambient{0.03f};                // fallback ambient when no IBL present
};

// ---- GPU (std140) representation ----
struct alignas(16) PointLightGpu {
    glm::vec4 posRange{0.0f};        // xyz position, w range
    glm::vec4 colorIntensity{0.0f};  // rgb colour, w intensity
};

struct alignas(16) LightingBlockGpu {
    glm::vec4 cameraPos{0.0f};          // xyz + pad
    glm::vec4 dirColor{0.0f};           // sun rgb + intensity in w
    glm::vec4 dirDirection{0.0f};       // sun direction xyz + pad
    glm::vec4 ambient{0.0f};            // ambient rgb + pad
    glm::ivec4 pointCount{0};           // active point-light count in x
    std::array<PointLightGpu, kMaxPointLights> points{};
};

// Lower the CPU-side description into the GPU block.
inline LightingBlockGpu PackLighting(const LightSetup& setup, const glm::vec3& cameraPos) {
    LightingBlockGpu b;
    b.cameraPos = glm::vec4(cameraPos, 1.0f);

    const DirectionalLight& s = setup.sun;
    const glm::vec3 dir = glm::length(s.direction) > 0.0f
                              ? glm::normalize(s.direction)
                              : glm::vec3(0.0f, -1.0f, 0.0f);
    b.dirColor = glm::vec4(s.color, s.intensity);
    b.dirDirection = glm::vec4(dir, 0.0f);

    b.ambient = glm::vec4(setup.ambient, 0.0f);

    const int n = static_cast<int>(std::min<std::size_t>(setup.points.size(), kMaxPointLights));
    b.pointCount = glm::ivec4(n, 0, 0, 0);
    for (int i = 0; i < n; ++i) {
        const PointLight& p = setup.points[i];
        b.points[i].posRange = glm::vec4(p.position, p.range);
        b.points[i].colorIntensity = glm::vec4(p.color, p.intensity);
    }
    for (int i = n; i < kMaxPointLights; ++i) {
        b.points[i] = PointLightGpu{};
    }
    return b;
}

} // namespace gfx

#endif // GFX_LIGHT_LIGHT_H
