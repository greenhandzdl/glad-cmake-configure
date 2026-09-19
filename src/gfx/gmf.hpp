#ifndef GFX_GMF_HPP
#define GFX_GMF_HPP

/**
 * @file gmf.hpp
 * @brief Shared GLOBAL MODULE FRAGMENT includes for the gfx named module.
 *
 * Every translation unit of module gfx (the primary interface gfx.cppm and each
 * `module gfx;` implementation unit) textually includes THIS header inside its
 * `module;` global module fragment, before the `module gfx;` / `export module
 * gfx;` directive. Doing so attaches GLAD, GLM and the common standard-library
 * entities to the *global module* rather than to module gfx — which is exactly
 * what we want: exported declarations then reference global-module GLuint /
 * glm::vec3, and main.cpp (which textually includes the same headers) sees the
 * identical global-module types instead of a conflicting module-scoped redeclaration.
 *
 * This superset only carries what is broadly needed to name the engine's public
 * types. A unit with extra needs (assimp / stb in the loaders, <mutex>/<future>
 * in the threaded assets code, <fstream> in Font) adds those honestly on top.
 */

// OpenGL loader + math: their types appear all over the public API.
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Common standard-library headers the engine types are expressed with.
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#endif // GFX_GMF_HPP
