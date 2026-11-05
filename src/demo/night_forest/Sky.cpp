// night_forest demo — the night sky cube: procedural star field and the
// equirectangular reprojection (see Sky.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Sky.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <glm/gtc/packing.hpp>

namespace night_forest {
namespace {

// Linear float -> IEEE754 half, one channel at a time.
//
// TextureCubeDesc's HDR path is the only 16-bit-float carrier the engine has, and
// nothing in the repository encodes to it (EnvironmentMap asks GL to do the
// conversion inside a compute-free prefilter pass), so the star field - which is
// authored on the CPU, in linear radiance, above 1.0 - needs the encoder here.
//
// GLM's packHalf2x16 is used rather than a hand-written exponent shuffle: it is
// the same conversion the drivers implement, it is already a dependency, and the
// one interesting case (subnormals below 2^-14) is exactly where a hand-rolled
// bit twiddle goes quietly wrong - a star at 6e-5 radiance becoming zero is not
// an error anybody sees, it is just a missing star.
std::uint16_t ToHalf(float v) {
    // Half's finite range ends at 65504; anything above that (a moon the user
    // scaled up, a NaN from a bad gain) saturates rather than becoming an
    // infinity, because an infinite texel in a cube map used as a reflection
    // propagates through the mix and can turn a whole crate face white.
    if (!(v > -65504.0f)) return v < 0.0f ? 0xFBFFu : 0x7BFFu;
    return static_cast<std::uint16_t>(glm::packHalf2x16(glm::vec2(v, 0.0f)) & 0xFFFFu);
}

// Integer hash for a lattice cell. MurmurHash3's 32-bit finaliser: cheap, and
// unlike a linear congruential mix it does not correlate small changes in the
// seed with small changes in the output, which is what stops a hashed star field
// from banding along the axes of its own grid.
std::uint32_t Hash(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed) {
    std::uint32_t h = seed;
    h ^= static_cast<std::uint32_t>(x) * 0x27d4eb2du;
    h  = (h << 13) | (h >> 19);
    h ^= static_cast<std::uint32_t>(y) * 0x165667b1u;
    h  = (h << 13) | (h >> 19);
    h ^= static_cast<std::uint32_t>(z) * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// One hash as a float in [0,1).
//
// The high 24 bits scaled by 2^-24, not `h % 100000 / 100000`. Two reasons, one of
// them performance and one of them taste: an integer division is the most expensive
// instruction in the whole per-texel loop (this runs a few hundred times per texel,
// and the field's cost is what decides the default cube resolution), and a modulo
// gives the lattice a preferred residue class - which is how a hashed star field
// ends up showing faint rows along the axes of its own grid.
float U01(std::int32_t x, std::int32_t y, std::int32_t z, std::uint32_t seed, int lane) {
    return static_cast<float>(Hash(x, y, z, seed + static_cast<std::uint32_t>(lane) * 0x9e3779b1u)
                              >> 8) * (1.0f / 16777216.0f);
}

// GLM's smoothstep for a scalar, spelled out: the star field and the moon's limb
// both need it, and it is three lines that no include is required to get right.
float Smoothstep(float e0, float e1, float x) {
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Value noise on the direction lattice: trilinear-interpolated hashes with
// smoothstep weights. Sampled through a unit direction, so it is seamless on the
// cube by construction - there is no seam to hide because nothing ever looks up
// a texture, it evaluates the same function at the same direction.
float Noise(const glm::vec3& d, float freq, std::uint32_t seed) {
    const glm::vec3 q = d * freq;
    const glm::vec3 f = q - glm::floor(q);
    const glm::ivec3 c = glm::ivec3(glm::floor(q));
    const glm::vec3 u = f * f * (3.0f - 2.0f * f);
    float v = 0.0f;
    for (int dz = 0; dz <= 1; ++dz)
        for (int dy = 0; dy <= 1; ++dy)
            for (int dx = 0; dx <= 1; ++dx) {
                const float wx = dx ? u.x : 1.0f - u.x;
                const float wy = dy ? u.y : 1.0f - u.y;
                const float wz = dz ? u.z : 1.0f - u.z;
                v += wx * wy * wz * U01(c.x + dx, c.y + dy, c.z + dz, seed, 0);
            }
    return v;
}

// Fractional Brownian motion over the direction lattice. The octave count is a
// parameter because the cost is measurable, not theoretical: every octave is eight
// hashes, the galaxy asks for two fbms per texel inside its band, and dropping the
// two of them from four octaves to three and two costs the frame nothing it can
// show - the fourth octave of the dust lands at a wavelength under one texel of
// the face, where the sampler averages it away.
float Fbm(const glm::vec3& d, float freq, std::uint32_t seed, int octaves = 4) {
    float sum = 0.0f, norm = 0.0f, amp = 1.0f;
    for (int oct = 0; oct < octaves; ++oct) {
        sum  += amp * Noise(d, freq, seed + static_cast<std::uint32_t>(oct) * 777u);
        norm += amp;
        amp  *= 0.5f;
        freq *= 2.07f;   // not exactly 2: a power-of-two ladder aliases against the
                        // lattice it is built on and shows the grid as faint cubes
    }
    return sum / norm;
}

// The three sky bands, linear radiance. Low by daylight standards - this is the
// floor the whole frame sits on, and the post chain's exposure has room to lift it.
constexpr glm::vec3 kZenith  {0.0055f, 0.0072f, 0.0185f};
constexpr glm::vec3 kMidSky  {0.0110f, 0.0150f, 0.0360f};
// The horizon and everything below it are the mist's own colour, and that is not a
// coincidence: the ground plane stops at 200 m and the mist finishes dissolving it
// at 190, so the band of sky behind the tree line is the one place the two surfaces
// are guaranteed to be adjacent. Authored as separate numbers they disagree by a
// factor of two and the horizon becomes a lit rim rather than a distance.
constexpr glm::vec3 kHorizon {0.0300f, 0.0420f, 0.0780f};
constexpr glm::vec3 kNadir   {0.0300f, 0.0420f, 0.0780f};

// The galactic plane's normal, tilted away from the cube's own axes so the band
// crosses several faces and the seam-free property is visible rather than
// assumed: a band that ran along one face's border would tile even if the code
// did not.
constexpr glm::vec3 kGalaxyNormal{0.42f, -0.78f, 0.46f};

// The moon: disc, limb darkening, two maria and the halo. Split out because the
// panorama path needs *only* this - an image may bring its own stars but it has
// no reason to bring the light the shading is going to point at, and the two
// paths must not disagree about where it is.
glm::vec3 SampleMoon(const glm::vec3& dir, const StarField& field) {
    if (field.moonIntensity <= 0.0f || field.moonRadius <= 0.0f) return glm::vec3(0.0f);
    const glm::vec3 m = MoonDirection(field);
    const float ang = std::acos(std::clamp(dot(dir, m), -1.0f, 1.0f));
    const float r = field.moonRadius;
    const float x = ang / r;

    glm::vec3 rgb = glm::vec3(0.0f);
    if (x < 1.05f) {
        // Maria at a lattice frequency chosen against the disc's own angular size,
        // not against the sky: the disc spans `r` radians and a Noise cell spans
        // 2/freq, so freq 90 puts five or six cells across it. The low frequencies
        // the galaxy uses would put one cell across the whole disc, and the disc
        // would come out as a flat coin.
        const float maria = Fbm(dir, 90.0f, field.seed ^ 0x9e3779b9u);
        const float limb  = std::sqrt(std::max(0.0f, 1.0f - x * x));
        const float edge  = 1.0f - Smoothstep(0.86f, 1.0f, x);
        rgb += glm::vec3(0.92f, 0.95f, 1.00f)
             * (field.moonIntensity * edge * (0.62f + 0.38f * limb) * (0.78f + 0.22f * maria));
    }
    // Halo: broad, cold, and far too bright to be physical - it is the sketch's
    // "the moon is this scene's sun", and the crate's reflection needs one bright
    // thing in the cube to mirror at all. The decay constant is per radian, so
    // 5.0 puts the visible glow inside ~11 degrees of the disc; the slower values
    // a first draft used (1.35, a 43-degree scale) lifted the whole hemisphere by
    // a few hundredths and turned "moonlit night" into "overcast dusk".
    rgb += glm::vec3(0.10f, 0.14f, 0.26f) * std::exp(-ang * 5.0f) * 0.35f
         * std::max(0.05f, field.moonIntensity / 14.0f);
    return rgb;
}

// One texel of the procedural field, stars excepted: everything here is a
// function of `dir`. The star light is *added* to this later, by the splat below,
// which is why nothing about a star appears in this function at all.
glm::vec3 SampleSkyBase(const glm::vec3& dir, const StarField& field) {
    // ---- base gradient: horizon -> mid -> zenith, and a flat floor below ----
    const float up = std::clamp(dir.y, 0.0f, 1.0f);
    glm::vec3 sky = up < 0.35f ? glm::mix(kHorizon, kMidSky, up / 0.35f)
                              : glm::mix(kMidSky, kZenith, (up - 0.35f) / 0.65f);
    // Below the horizon there is nothing to gradient: that half of the cube is
    // only ever seen past the edge of the ground plane, and it is the mist's
    // colour there or it is a visible seam. kNadir is kHorizon for that reason.
    if (dir.y < 0.0f) sky = kNadir;

    // ---- the milky way: a band along one great circle, broken into clumps ----
    // The two noise frequencies are absolute, not fractions of `field.size`: the
    // rifts and clumps are an angular structure, and tying them to the texel count
    // would make the galaxy finer-grained just because the cube got bigger - the
    // one thing a resolution parameter must never do to the content.
    const float gn = 1.0f / std::sqrt(kGalaxyNormal.x * kGalaxyNormal.x +
                                      kGalaxyNormal.y * kGalaxyNormal.y +
                                      kGalaxyNormal.z * kGalaxyNormal.z);
    const glm::vec3 gax = kGalaxyNormal * gn;
    const float off = std::asin(std::clamp(dot(dir, gax), -1.0f, 1.0f));
    const float band = std::exp(-(off * off) / (2.0f * 0.11f * 0.11f));

    glm::vec3 rgb = sky;
    // Both fbms are only ever multiplied by `band`, so past ~17 degrees from the
    // great circle the galaxy is not merely dim, it is under the quarter-bit the
    // half-float stores - skipping the noise there gives away nothing a texel could
    // show and saves a third of this pass (measured, 512 faces, unoptimised build:
    // 2.28 s for the base pass with the test written as 0.002 and four octaves each,
    // 1.58 s as written below). The other second is not noise at all: it is the one
    // direction and the one moon tap every texel costs, which is why the resolution
    // and not the galaxy is what a bigger cube actually buys.
    if (band > 0.02f) {
        // dust: the rifts are what make it read as a galaxy rather than as a stripe.
        // Three octaves, not four - the fourth lands at a wavelength under one texel
        // of a 512 face, where the sampler averages it to nothing anyway.
        const float dust = Fbm(dir, 24.0f, field.seed ^ 0x51ed2701u, 3);
        // clumps: two octaves, because the structure wanted here is the big scale.
        const float clump = Fbm(dir, 5.0f, field.seed ^ 0x27d4eb2fu, 2);
        rgb += glm::mix(glm::vec3(0.085f, 0.095f, 0.175f),
                        glm::vec3(0.165f, 0.130f, 0.095f), dust)
             * band * field.galaxy * (0.35f + 0.9f * clump);
    }

    rgb += SampleMoon(dir, field);
    return rgb;
}

// ---------------------------------------------------------------------------
// The stars: generated once per texel of the cube and splatted into the texels
// around them, instead of gathered per texel from a neighbourhood of cells.
//
// The gather is the obvious formulation and it has two costs that only become
// visible on screen. Its search radius is one lattice cell, so a star whose
// gaussian still carries value a cell further out is not drawn softly - it is
// *cut off* at the cell border, which renders every bright point as a square and
// tiles the sky with a faint mesh of those borders; keeping the cut invisible
// pins a star under half a texel wide, and one texel of a 512 face is five pixels
// on this window, so "under half a texel" is a hard-edged square however it is
// spelled. It also evaluates 27 hashes for every one of the cube's 1.5 million
// texels - 41 million of them, to find the fifteen hundred stars that are there.
//
// The splat inverts both. The work is stars times footprint - fifteen hundred stars,
// a hundred-odd texels each - rather than texels times 27, and because the
// footprint comes from the angle between two directions rather than from a cell
// index, a star may be as wide as it wants and it crosses the six seams correctly
// without a special case: each face is asked what its own texels see, and the
// halves add up. What that honesty does cost is that a star sitting exactly on a
// face border is generated twice, once per face, and lands twice. At the shipped
// density that is a handful of points in the whole sky, and the only consequence
// is that one of them is brighter than the magnitude distribution promises. A
// real sky has brighter stars, and nobody has ever found the seam in one.
// ---------------------------------------------------------------------------
struct Star {
    glm::vec3 dir;      // unit direction of the centre
    glm::vec3 tint;
    float flux;         // HDR radiance at the peak
    float sigma;        // angular width, radians
};

// A face spans Pi/2 radians across `n` texels.
constexpr float kFaceAngle = 1.5707963267948966f;

// Add one star to every face it touches. The contribution is evaluated from the
// true angle between the texel's direction and the star's, so the footprint is a
// disc on the sphere - which is the same thing as a disc on the screen, and the
// only reason a CPU star field usually is not one.
//
// The inner loop is scalar and closed-form, and that is not decoration: the three
// obvious lines - `dot(FaceTexelCenter(face, i, j, n), s.dir)` - measured 5.2 s
// of this demo's 7.1 s startup, because every texel of every box rebuilt three
// vectors out of a table, normalised one and took an acos of the result, and an
// unoptimised build charges for each of those as a call. Written the way below a
// star costs a few dozen multiplies and the splat leaves the profile altogether.
//
// The identity that makes it scalar is the face basis being orthonormal. For the
// unnormalised texel vector w = fwd + s*right + t*up,
//
//     |w|^2    = 1 + s^2 + t^2                    (cross terms vanish)
//     w . sd   = a * (1 + s*bs + t*bt)            a = fwd.sd, bs = right.sd / a
//
// so the cosine of the angle between the texel and the star needs no vector at
// all, and for the fraction of a degree a star's footprint spans, 2*(1 - cos) is
// the squared chord, which stands in for the squared angle to better than one part
// in 10^5 - that substitution is what removes the acos. One exp per lit texel is
// left, and the exp is the star.
void SplatStar(std::vector<glm::vec3>& rgb, int n, const Star& s) {
    const float reach = 2.6f * s.sigma;                       // radians, cut-off
    const float cutChord2 = 2.0f * (1.0f - std::cos(reach));  // same cut, in chords
    const float invSig2 = 1.0f / (s.sigma * s.sigma);
    const float step = 2.0f / static_cast<float>(n);
    const std::size_t faceTexels = static_cast<std::size_t>(n) * n;
    for (int face = 0; face < 6; ++face) {
        const glm::vec3 fwd = CubeFaceAxis(face, 2);
        const float a = dot(s.dir, fwd);
        if (a <= 1e-4f) continue;                             // behind this face
        // Gnomonic projection: the ratios, not the components, place the centre,
        // because normalising a direction cancels the common factor.
        const float bs = dot(s.dir, CubeFaceAxis(face, 0)) / a;
        const float bt = dot(s.dir, CubeFaceAxis(face, 1)) / a;
        // The face only covers the pyramid |b|,|t| <= 1, so a star outside it by
        // more than its own footprint has nothing to draw here. The margin is four
        // times the reach rather than one because the gnomonic angle shrinks as b
        // grows - at worst by a factor of three, at the corner - and a star just
        // outside that window still has to land on the neighbour it belongs to.
        // Without this test a star grazing the plane of a face is projected to
        // |b| of ten thousand, the box below is computed at half a million texels,
        // `static_cast<int>` of it is undefined, and the loop writes past the end of
        // the buffer: which is how a star field turns into a black rectangle and a
        // corrupt heap rather than a sky.
        const float slack = 1.0f + 4.0f * reach;
        if (std::abs(bs) > slack || std::abs(bt) > slack) continue;
        // One texel is (Pi/2)/n radians at the face centre and the grid stretches
        // toward the edges, roughly by 1/a^2, so the box has to grow with the
        // stretch or a star in a corner is clipped by its own face. Generous by
        // construction: an extra texel costs two multiplies, a missing one cuts a
        // disc - and the exact test below rejects the corners anyway.
        const float rTex = reach * static_cast<float>(n) * 0.5f / (a * a) + 1.0f;
        const int r = std::min(n, static_cast<int>(std::ceil(rTex)));
        const float fn = static_cast<float>(n);
        const int ci = static_cast<int>(std::floor((bs + 1.0f) * 0.5f * fn));
        const int cj = static_cast<int>(std::floor((bt + 1.0f) * 0.5f * fn));
        if (ci + r < 0 || cj + r < 0 || ci - r >= n || cj - r >= n) continue;

        const std::size_t base = static_cast<std::size_t>(face) * faceTexels;
        const int i0 = std::max(0, ci - r), i1 = std::min(n - 1, ci + r);
        const int j0 = std::max(0, cj - r), j1 = std::min(n - 1, cj + r);
        for (int j = j0; j <= j1; ++j) {
            const float t = (static_cast<float>(j) + 0.5f) * step - 1.0f;
            const float at = a * (1.0f + t * bt);    // the row's constant part
            const float as = a * bs;                  // ... and the slope along it
            const float t2 = t * t;
            std::size_t off = base + static_cast<std::size_t>(j) * n + i0;
            for (int i = i0; i <= i1; ++i, ++off) {
                const float sv = (static_cast<float>(i) + 0.5f) * step - 1.0f;
                const float cd = (at + as * sv) / std::sqrt(1.0f + sv * sv + t2);
                const float chord2 = 2.0f * (1.0f - cd);
                if (chord2 > cutChord2) continue;
                // Clamped because the texel that contains the star's own centre
                // computes cd a hair over one: negative here means a few parts in
                // 10^7, but it is the one input that can turn into an exponent of
                // its own sign, and an exp that overflows is a star-shaped hole in
                // the sky.
                rgb[off] += s.tint * (s.flux * std::exp(-std::max(0.0f, chord2) * invSig2));
            }
        }
    }
}

// Walk the cube's texels and let each one decide, from its own hash, whether it
// holds a star. The placement is stratified - one candidate per texel, so no two
// stars can sit closer than about one texel unless they are on neighbouring cells -
// but the jitter is wider than the cell. Kept inside the cell it would be a grid:
// one texel of a 512 face is five pixels on this window, and a field of points on a
// five-pixel lattice reads as a texture rather than as a sky. Three texels of play
// breaks the lattice visibly and is still small enough that the stratification
// keeps the field from clumping, which is the only thing stratification was for.
void SplatStars(const StarField& field, std::vector<glm::vec3>& rgb, int n) {
    const float density = std::clamp(field.density, 0.0f, 0.95f);
    const float texelAngle = kFaceAngle / static_cast<float>(n);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                if (U01(i, j, face, field.seed, 1) > density) continue;
                Star s;
                const float ci = static_cast<float>(i) + (U01(i, j, face, field.seed, 2) - 0.5f) * 3.0f;
                const float cj = static_cast<float>(j) + (U01(i, j, face, field.seed, 3) - 0.5f) * 3.0f;
                s.dir = CubeFaceTexelDirection(face, ci, cj, n);
                const float mag = U01(i, j, face, field.seed, 5);
                // A magnitude distribution, not a uniform one: real skies put most
                // of their light in a few stars, and here the shape also decides
                // what the bloom does. The post chain blooms everything above its
                // threshold, so with a flat spread four fifths of the field is a
                // light source and five thousand overlapping glows read as a milky
                // sheet rather than as a night sky.
                const float m3 = mag * mag * mag;
                s.flux = field.brightness * (0.05f + 0.95f * m3 * m3);
                // Width with magnitude, as it is in a photograph - the bright ones
                // are bigger, not only taller, because the eye and the sensor both
                // integrate over an area. The scale is texels of the face, and a
                // texel is five pixels here, so this is deliberately tight: a star
                // two texels wide is a disc no viewer reads as a star.
                s.sigma = (0.4f + 0.8f * mag * mag) * texelAngle;
                // Spectral colour: the bulk white-blue, the brightest few warm.
                // Counter-intuitive for a lamp, correct for a star - and it gives
                // the bloom something other than one hue to spread.
                const float temp = U01(i, j, face, field.seed, 6);
                s.tint = glm::mix(glm::vec3(0.72f, 0.82f, 1.00f),
                                  glm::vec3(1.00f, 0.84f, 0.66f), temp * temp);
                SplatStar(rgb, n, s);
            }
        }
    }
}

// Bilinear tap of a decoded panorama in its own row-major byte grid. `u` wraps
// (the panorama is continuous in longitude; the column at u=1 is the column at
// u=0), `v` clamps (the poles are single points and pulling them would be a lie).
void TapPanorama(const gldx::Texture2DDesc& img, float u, float v, glm::vec3& out) {
    const float fx = u * static_cast<float>(img.width);
    const float fy = (1.0f - v) * static_cast<float>(img.height) - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float wx = fx - static_cast<float>(x0);
    const float wy = fy - static_cast<float>(y0);
    const int w = img.width, h = img.height;
    const int ch = std::max(1, std::min(4, img.channels));

    auto at = [&](int xx, int yy) {
        const int cx = ((xx % w) + w) % w;
        const int cy = std::clamp(yy, 0, h - 1);
        const std::uint8_t* p = img.pixels.data() +
            (static_cast<std::size_t>(cy) * w + cx) * static_cast<std::size_t>(ch);
        return glm::vec3(p[0] / 255.0f,
                         ch > 2 ? p[1] / 255.0f : p[0] / 255.0f,
                         ch > 2 ? p[2] / 255.0f : p[0] / 255.0f);
    };
    out = glm::vec3(0.0f);
    out += at(x0, y0)     * ((1.0f - wx) * (1.0f - wy));
    out += at(x0 + 1, y0) * (wx          * (1.0f - wy));
    out += at(x0, y0 + 1) * ((1.0f - wx) * wy);
    out += at(x0 + 1, y0 + 1) * (wx      * wy);
}

// sRGB encoded value -> linear. The engine's textures use GL_SRGB8_ALPHA8 and let
// the sampler do this; a cube handed to GL_RGB16F has no such decoder, so a
// panorama is decoded here or the whole sky arrives a stop too dark and the
// stars - which the image already stores near the floor - collapse into black.
float SrgbToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

} // namespace

glm::vec3 MoonDirection(const StarField& field) {
    const float len = glm::length(field.moonDir);
    return len > 1e-4f ? field.moonDir / len : glm::vec3(0.0f, 1.0f, 0.0f);
}

gldx::TextureCubeDesc MakeStarCubeDesc(const StarField& field) {
    gldx::TextureCubeDesc desc;
    desc.size = std::max(16, field.size);
    desc.hdr  = true;
    const int n = desc.size;
    const std::size_t faceTexels = static_cast<std::size_t>(n) * n;

    // The sky is painted into floats and only encoded to half-float at the end.
    // Not for precision's sake in the abstract: the stars are *added* to a texel
    // that already holds sky and galaxy, and a field this faint sits in the last
    // two bits of the format, so quantising per add would put visible steps along
    // the gradients. 19 MB of scratch for the lifetime of this call is the cheap
    // way to not have to think about that.
    std::vector<glm::vec3> rgb(faceTexels * 6);
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < n; ++j) {
            for (int i = 0; i < n; ++i) {
                rgb[static_cast<std::size_t>(face) * faceTexels
                    + static_cast<std::size_t>(j) * n + i]
                    = SampleSkyBase(FaceTexelCenter(face, i, j, n), field);
            }
        }
    }

    SplatStars(field, rgb, n);

    desc.half.assign(faceTexels * 6 * 4, 0);
    for (std::size_t t = 0; t < faceTexels * 6; ++t) {
        const glm::vec3& c = rgb[t];
        desc.half[t * 4 + 0] = ToHalf(c.x);
        desc.half[t * 4 + 1] = ToHalf(c.y);
        desc.half[t * 4 + 2] = ToHalf(c.z);
        desc.half[t * 4 + 3] = ToHalf(1.0f);
    }
    return desc;
}

bool LoadEquirectCubeDesc(const std::string& path, const StarField& field,
                          gldx::TextureCubeDesc& out, std::string& err) {
    auto img = gldx::LoadImageToDesc(path, 0);
    if (!img) {
        err = img.error();
        return false;
    }
    const int w = img->width, h = img->height;
    // 2:1 is the definition of an equirectangular panorama. The tolerance is the
    // latitude band's own distortion, not sloppiness: a cube-map sheet laid out
    // as 6 x 1 would also be 6:1 and must not be accepted, while a 1920x960 JPEG
    // with one cropped row still reprojects correctly.
    if (w < 64 || h < 32 || std::abs(w - 2 * h) > h / 2) {
        err = "not an equirectangular panorama (expected a 2:1 image, got "
            + std::to_string(w) + "x" + std::to_string(h) + ")";
        return false;
    }
    if (img->channels != 3 && img->channels != 4) {
        err = "expected an RGB or RGBA image";
        return false;
    }

    StarField f = field;
    if (f.gain <= 0.0f) f.gain = 1.0f;
    // Allocated here rather than filled: the procedural field is not added to the
    // image below, only the moon is, so running MakeStarCubeDesc would cost a
    // second full pass and paint stars the user's panorama already has.
    out.size = std::max(16, f.size);
    out.hdr  = true;
    out.half.assign(static_cast<std::size_t>(6) * out.size * out.size * 4, 0);

    const float tau = 6.283185307179586f;
    const float pi  = 3.141592653589793f;
    for (int face = 0; face < 6; ++face) {
        for (int j = 0; j < out.size; ++j) {
            for (int i = 0; i < out.size; ++i) {
                const glm::vec3 dir = FaceTexelCenter(face, i, j, out.size);
                const float u = std::atan2(dir.z, dir.x) / tau + 0.5f;
                const float v = std::asin(std::clamp(dir.y, -1.0f, 1.0f)) / pi + 0.5f;
                glm::vec3 tap;
                TapPanorama(*img, u, v, tap);
                tap = glm::vec3(SrgbToLinear(tap.x), SrgbToLinear(tap.y), SrgbToLinear(tap.z));

                const std::size_t off = (static_cast<std::size_t>(face) * out.size * out.size
                                         + static_cast<std::size_t>(j) * out.size + i) * 4;
                // max rather than +: the image may already contain a moon, and a
                // disc added on top of a brighter one in the picture would bloom
                // into a blob twice the size. Whichever light source is stronger at
                // this direction wins, and both are where the shading says they are.
                const glm::vec3 rgb = glm::max(tap * f.gain, SampleMoon(dir, f));
                out.half[off + 0] = ToHalf(rgb.x);
                out.half[off + 1] = ToHalf(rgb.y);
                out.half[off + 2] = ToHalf(rgb.z);
                out.half[off + 3] = ToHalf(1.0f);
            }
        }
    }
    std::printf("night_forest: sky reprojected from %s (%dx%d)\n", path.c_str(), w, h);
    return true;
}

} // namespace night_forest
