#ifndef GLDX_SHADOW_SHADOWDATA_H
#define GLDX_SHADOW_SHADOWDATA_H

/**
 * @file ShadowData.h
 * @brief std140 GPU block describing cascaded shadow maps (no GL).
 *
 * Mirrors the GLSL `layout(std140, binding = kBinding) uniform ShadowBlock`.
 * Each cascade contributes a light-space bias-adjusted view-projection matrix
 * plus its view-space far split. A mat4 array is 64 B/element in both std140
 * and the C++ layout below, so no manual padding is required.
 */

#include <array>
#include <glm/glm.hpp>

namespace gldx {

inline constexpr int kCascadeCount = 4;

struct alignas(16) ShadowBlockGpu {
    std::array<glm::mat4, kCascadeCount> lightMat{};  // world -> light-clip
    glm::vec4 cascadeSplits{0.0f};                     // view-space far distance per cascade
    glm::vec4 params{0.0f};                            // x = cascade count, y = texel size
};

} // namespace gldx

#endif // GLDX_SHADOW_SHADOWDATA_H
