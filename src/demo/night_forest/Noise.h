#ifndef NIGHT_FOREST_NOISE_H
#define NIGHT_FOREST_NOISE_H

/**
 * @file Noise.h
 * @brief The demo's single source of procedural noise: one integer hash, the value
 *        noise built on it, and the octave sums on top of that.
 *
 * Two halves of the scene read these functions - the grass map's clumps, tufts and
 * blades (Scene.cpp) and the height field's hills (Terrain.cpp) - and they share
 * one implementation for a reason that is visible rather than stylistic: the ground
 * map is tiled across the same metres the terrain grid spans, so if the bumps and
 * the texture came from unrelated noise sources the shading would disagree with the
 * relief it is painted on, which reads as a surface lit from somewhere else.
 *
 * Everything here is a pure function of lattice coordinates and a seed. No RNG
 * state is carried between calls and no frame counter enters, which is what makes a
 * procedurally generated map or terrain bit-reproducible - the demo's screenshot
 * contract (main.cpp's header states the two conditions that keep it).
 *
 * Two families live side by side and the difference matters:
 *   P* - periodic. Wraps its lattice at `freq`, so the tile is seamless under
 *        GL_REPEAT. Required for a texture that repeats 48 times across a field;
 *        meaningless for a terrain that has edges.
 *   Plain Noise/Fbm - aperiodic. Neighbouring cells are independent, which is what
 *        an image sampled at world metres looks like: wrapping the height lattice
 *        would glue the map's east edge to its west edge and carve a cliff there.
 *
 * NOTE: raw arithmetic only - no gldx types - so this header may be included
 * before or after `import gldx;`.
 */

#include <cmath>
#include <cstdint>

namespace night_forest {
namespace noise {

// A 32-bit integer hash, used for both the speckle decisions in the maps and the
// periodic lattice below.
inline std::uint32_t Hash2(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 0x1f1f1f1fu);
    h = (h << 11) ^ (h >> 5) ^ static_cast<std::uint32_t>(y) * 0x27d4eb2du;
    h += h * 3u; h ^= h >> 16;
    h *= 0x85ebca6bu; h ^= h >> 13;
    return h;
}

inline float U01_2(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    return static_cast<float>(Hash2(x, y, seed) % 100000u) / 100000.0f;
}

inline float Smooth01(float t) { return t * t * (3.0f - 2.0f * t); }

// ---------------------------------------------------------------------------
// Periodic family - for surfaces that tile.
// ---------------------------------------------------------------------------

// Value noise that repeats with period 1 in both axes.
//
// The wrap is not cosmetic: Texture2D::Upload sets GL_REPEAT, so a map whose left
// edge disagrees with its right edge draws a visible grid across the ground every
// 48 tiles. Wrapping the lattice indices (not the output) makes the two edges
// sample the same cells, which is the cheapest way to be seamless.
inline float PNoise(float u, float v, int freq, std::uint32_t seed) {
    const float fx = u * static_cast<float>(freq), fy = v * static_cast<float>(freq);
    const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
    const float tx = Smooth01(fx - static_cast<float>(x0));
    const float ty = Smooth01(fy - static_cast<float>(y0));
    auto at = [&](int dx, int dy) {
        const int ix = ((x0 + dx) % freq + freq) % freq;
        const int iy = ((y0 + dy) % freq + freq) % freq;
        return U01_2(ix, iy, seed);
    };
    const float a = at(0, 0) + (at(1, 0) - at(0, 0)) * tx;
    const float b = at(0, 1) + (at(1, 1) - at(0, 1)) * tx;
    return a + (b - a) * ty;
}

inline float PFbm(float u, float v, int freq, std::uint32_t seed, int octaves) {
    float sum = 0.0f, norm = 0.0f, amp = 1.0f;
    int f = freq;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * PNoise(u, v, f, seed + static_cast<std::uint32_t>(o) * 131u);
        norm += amp;
        amp *= 0.5f;
        f *= 2;
    }
    return sum / norm;
}

// ---------------------------------------------------------------------------
// Aperiodic family - for a field with edges.
// ---------------------------------------------------------------------------

// Value noise on an integer lattice, in [0,1), sampled at continuous `x` / `y`.
// Nothing wraps: the cell at (-1, 3) is as random as the one at (197, 3), which is
// what a height field wants and what the periodic version above deliberately is not.
inline float Noise2D(float x, float y, std::uint32_t seed) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float tx = Smooth01(x - static_cast<float>(x0));
    const float ty = Smooth01(y - static_cast<float>(y0));
    auto at = [&](int dx, int dy) {
        return U01_2(x0 + dx, y0 + dy, seed);
    };
    const float a = at(0, 0) + (at(1, 0) - at(0, 0)) * tx;
    const float b = at(0, 1) + (at(1, 1) - at(0, 1)) * tx;
    return a + (b - a) * ty;
}

// Octaves of the above, normalised to [0,1) so the caller's relief factor is the
// only thing that decides how tall the result is. `wave` is the wavelength of the
// coarsest octave in the caller's units (metres here), and each octave halves it.
inline float Fbm2D(float x, float y, float wave, std::uint32_t seed, int octaves) {
    float sum = 0.0f, norm = 0.0f, amp = 1.0f;
    float w = wave > 1e-3f ? wave : 1e-3f;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * Noise2D(x / w, y / w, seed + static_cast<std::uint32_t>(o) * 131u);
        norm += amp;
        amp *= 0.5f;
        w *= 0.5f;
    }
    return sum / norm;
}

} // namespace noise
} // namespace night_forest

#endif // NIGHT_FOREST_NOISE_H
