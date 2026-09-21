#ifndef GLDX_LIGHT_LIGHT_H
#define GLDX_LIGHT_LIGHT_H

/**
 * @file Light.h
 * @brief CPU-side light data + a std140-compatible GPU block (no GL).
 *
 * LightSetup is the artist/game-facing description (one directional "sun" that
 * drives cascaded shadows + a bounded list of point lights, plus an optional
 * linear distance fog). PackLighting() lowers it into LightingBlockGpu, whose
 * layout mirrors the GLSL `layout(std140) uniform LightingBlock` consumed by
 * the PBR / instanced / voxel shaders. Because every GPU-block member is a
 * 16-byte vec4/ivec4, the C++ struct needs no manual padding to match std140.
 *
 * The fog members are appended at the end of the block: a GLSL uniform block
 * may be declared with fewer members than the buffer holds, so older programs
 * keep working, and PackLighting() leaves fog disabled unless the caller sets
 * fogStart/fogEnd explicitly — existing demos render bit-identically.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace gldx {

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
    // Linear distance fog, applied in the same place as the shading output
    // (before the post-process tone map), so fogColor lives in linear HDR
    // space like everything else the fragment stage writes — matching the
    // skybox horizon colour is what makes distant geometry dissolve instead
    // of sitting on a grey band. Disabled while fogStart <= 0 or
    // fogEnd <= fogStart.
    glm::vec3 fogColor{0.35f, 0.5f, 0.65f};
    float fogStart = 0.0f;
    float fogEnd   = 0.0f;
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
    glm::vec4 fogColor{0.0f};           // linear rgb + pad
    glm::vec4 fogParams{0.0f};          // x = enabled, y = start, z = end
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

    // Fog is opt-in: any interval that cannot produce a gradient keeps the
    // shader on the disabled branch, so callers who never touched these
    // fields see exactly the previous image.
    const bool fogOn = setup.fogStart > 0.0f && setup.fogEnd > setup.fogStart;
    b.fogColor = glm::vec4(setup.fogColor, 0.0f);
    b.fogParams = glm::vec4(fogOn ? 1.0f : 0.0f, setup.fogStart, setup.fogEnd, 0.0f);
    return b;
}

} // namespace gldx

#endif // GLDX_LIGHT_LIGHT_H
