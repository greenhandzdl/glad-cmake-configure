// Scene constants and the linear->display transform, shared by the passes.
//
// Split out of main.cpp so the passes and the setup code can name the same palette.
// NOTE: names gldx/glm types, so include AFTER `import gldx;`.
#pragma once

#include <glm/glm.hpp>

namespace anim {

constexpr float kPi = 3.14159265358979323846f;

constexpr glm::vec3 kSkyColor{0.30f, 0.44f, 0.62f};
constexpr glm::vec3 kGroundColor{0.20f, 0.21f, 0.23f};
constexpr glm::vec3 kBounceColor{0.08f, 0.07f, 0.06f};
constexpr glm::vec3 kSunColor{1.00f, 0.94f, 0.84f};
constexpr glm::vec3 kSunDir = {-0.38f, -0.82f, -0.42f};   // travels toward the scene
constexpr float     kSunIntensity = 3.4f;

glm::vec3 AcesFilm(glm::vec3 x) {
    const glm::vec3 a = x * (2.51f * x + 0.03f);
    const glm::vec3 b = x * (2.43f * x + 0.59f) + 0.14f;
    return glm::clamp(a / b, glm::vec3(0.0f), glm::vec3(1.0f));
}

// The linear->display transform both shaders end with, mirrored on the CPU for
// the clear colour so the horizon fade and the background cannot disagree (a
// mismatch there is a visible band exactly where the floor runs out).
glm::vec3 DisplayColor(glm::vec3 linear) {
    return glm::pow(AcesFilm(linear * 1.15f), glm::vec3(1.0f / 2.2f));
}

// ---------------------------------------------------------------------------

}  // namespace anim
