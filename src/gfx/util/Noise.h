#ifndef GFX_UTIL_NOISE_H
#define GFX_UTIL_NOISE_H

/**
 * @file Noise.h
 * @brief Seedable classic Perlin noise (2D/3D) + fBm fractal summation.
 *
 * The procedural-content helper the voxel terrain path needs (heightmaps,
 * ore scattering, texture jitter). One Noise instance owns its own permutation
 * table built from a seed, so instances are deterministic across runs and
 * platforms (integer hashing only, no FP order dependence), and carrying no
 * mutable state they are safe to use from worker threads (plan: meshing/
 * generation jobs run off the render thread).
 *
 * Output ranges: Perlin* ~ [-1, 1], Fbm* ~ [-1, 1] (normalised by the summed
 * amplitude). Perlin is the improved (Ken Perlin 2002) lattice with a smooth
 * quintic fade and 12-edge gradient selection.
 */

#include <array>
#include <cstdint>

namespace gfx {

class Noise {
public:
    // A seed builds a shuffled 512-entry permutation table (the classic
    // double-wide layout, so index math never needs a mask of the modulus).
    explicit Noise(std::uint32_t seed = 0u);

    [[nodiscard]] double Perlin2(double x, double y) const;
    [[nodiscard]] double Perlin3(double x, double y, double z) const;

    // Fractal Brownian motion: `octaves` Perlin layers with increasing
    // frequency (x lacunarity) and decreasing amplitude (x gain) per octave.
    // The result is divided by the total amplitude so it stays in ~[-1, 1].
    [[nodiscard]] double Fbm2(double x, double y, int octaves = 4,
                              double lacunarity = 2.0, double gain = 0.5) const;
    [[nodiscard]] double Fbm3(double x, double y, double z, int octaves = 4,
                              double lacunarity = 2.0, double gain = 0.5) const;

    // Remap helper for callers: [-1,1] -> [0,1] with clamping.
    [[nodiscard]] static double ToUnit(double perlinValue) {
        return (perlinValue < -1.0 ? 0.0 : (perlinValue > 1.0 ? 1.0 : (perlinValue + 1.0) * 0.5));
    }

private:
    std::array<std::uint8_t, 512> perm_{};
};

} // namespace gfx

#endif // GFX_UTIL_NOISE_H
