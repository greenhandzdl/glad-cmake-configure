module;

#include "gfx/gmf.hpp"

#include <random>

module gfx;

namespace gfx {

namespace {

constexpr double Fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
constexpr double Lerp(double a, double b, double t) { return a + t * (b - a); }

// Ken Perlin's 12-edge gradient set, indexed by a hash value.
double Grad2(std::uint8_t hash, double x, double y) {
    switch (hash & 0x0F) {
        case 0x0: return  x + y;   case 1: return -x + y;
        case 2: return  x - y;     case 3: return -x - y;
        case 4: return  x;         case 5: return -x;
        case 6: return  y;         case 7: return -y;
        default:                  return 0.0;   // unused halves collapse to 2D
    }
}

double Grad3(std::uint8_t hash, double x, double y, double z) {
    const std::uint8_t h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

} // namespace

Noise::Noise(std::uint32_t seed) {
    std::mt19937 rng(seed);
    for (int i = 0; i < 256; ++i) perm_[i] = static_cast<std::uint8_t>(i);
    for (int i = 255; i > 0; --i) {           // Fisher-Yates with the seeded rng
        std::uniform_int_distribution<int> dist(0, i);
        const int j = dist(rng);
        const std::uint8_t t = perm_[i];
        perm_[i] = perm_[j];
        perm_[j] = t;
    }
    for (int i = 0; i < 256; ++i) perm_[256 + i] = perm_[i];
}

double Noise::Perlin2(double x, double y) const {
    const auto floorMask = [](double v, int& cell) {
        cell = static_cast<int>(std::floor(v));
        return v - cell;
    };
    int cx, cy;
    const double fx = floorMask(x, cx);
    const double fy = floorMask(y, cy);
    const int X = cx & 255, Y = cy & 255;

    const double u = Fade(fx), v = Fade(fy);
    const std::uint8_t aa = perm_[perm_[X] + Y];
    const std::uint8_t ab = perm_[perm_[X] + Y + 1];
    const std::uint8_t ba = perm_[perm_[X + 1] + Y];
    const std::uint8_t bb = perm_[perm_[X + 1] + Y + 1];

    const double x1 = Lerp(Grad2(aa, fx, fy),     Grad2(ba, fx - 1.0, fy),     u);
    const double x2 = Lerp(Grad2(ab, fx, fy - 1.0), Grad2(bb, fx - 1.0, fy - 1.0), u);
    return Lerp(x1, x2, v);
}

double Noise::Perlin3(double x, double y, double z) const {
    const auto floorMask = [](double v, int& cell) {
        cell = static_cast<int>(std::floor(v));
        return v - cell;
    };
    int cx, cy, cz;
    const double fx = floorMask(x, cx);
    const double fy = floorMask(y, cy);
    const double fz = floorMask(z, cz);
    const int X = cx & 255, Y = cy & 255, Z = cz & 255;

    const double u = Fade(fx), v = Fade(fy), w = Fade(fz);
    const int A  = perm_[X] + Y,  AA = perm_[A] + Z,  AB = perm_[A + 1] + Z;
    const int B  = perm_[X + 1] + Y, BA = perm_[B] + Z, BB = perm_[B + 1] + Z;

    return Lerp(
        Lerp(Lerp(Grad3(perm_[AA],     fx,      fy,      fz),
                  Grad3(perm_[BA],     fx - 1.0, fy,      fz), u),
             Lerp(Grad3(perm_[AB],     fx,      fy - 1.0, fz),
                  Grad3(perm_[BB],     fx - 1.0, fy - 1.0, fz), u), v),
        Lerp(Lerp(Grad3(perm_[AA + 1], fx,      fy,      fz - 1.0),
                  Grad3(perm_[BA + 1], fx - 1.0, fy,      fz - 1.0), u),
             Lerp(Grad3(perm_[AB + 1], fx,      fy - 1.0, fz - 1.0),
                  Grad3(perm_[BB + 1], fx - 1.0, fy - 1.0, fz - 1.0), u), v),
        w);
}

double Noise::Fbm2(double x, double y, int octaves, double lacunarity, double gain) const {
    double sum = 0.0, amp = 1.0, freq = 1.0, norm = 0.0;
    const int n = octaves < 1 ? 1 : octaves;
    for (int i = 0; i < n; ++i) {
        sum  += amp * Perlin2(x * freq, y * freq);
        norm += amp;
        amp  *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

double Noise::Fbm3(double x, double y, double z, int octaves, double lacunarity, double gain) const {
    double sum = 0.0, amp = 1.0, freq = 1.0, norm = 0.0;
    const int n = octaves < 1 ? 1 : octaves;
    for (int i = 0; i < n; ++i) {
        sum  += amp * Perlin3(x * freq, y * freq, z * freq);
        norm += amp;
        amp  *= gain;
        freq *= lacunarity;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

} // namespace gfx
