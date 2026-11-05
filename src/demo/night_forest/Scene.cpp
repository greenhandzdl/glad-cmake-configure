// night_forest demo — the instanced world: tree geometry and scatter, the crate's
// roam table, procedural ground and plank maps (see Scene.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace night_forest {
namespace {

// ---------------------------------------------------------------------------
// Mesh assembly. Everything below is Stage A: MeshData in, MeshData out, no GL.
// ---------------------------------------------------------------------------

// Append `part` to `dst`, transformed by `xf` and tinted by `color`.
//
// The colour multiplies rather than replaces, so a part that already carries its
// own vertex colours (a cone, a plank face) keeps them and a caller may still
// pale one whole tree. Normals go through the affine inverse-transpose rather
// than mat3(xf) because the broadleaf canopy is deliberately squashed
// non-uniformly and this is the one place in the demo where an inverse is
// available. The instance path cannot say the same - the vertex stage has no
// inverse() in GLSL 4.10 - which is exactly why shape-level squash belongs in
// the mesh and only isotropic scale belongs on the instance.
void AddPart(gldx::MeshData& dst, const gldx::MeshData& part, const glm::mat4& xf,
             const glm::vec4& color) {
    const glm::mat3 nm = glm::affineInverse(xf);
    const std::uint32_t base = static_cast<std::uint32_t>(dst.vertices.size());

    dst.vertices.reserve(dst.vertices.size() + part.vertices.size());
    for (const gldx::Vertex& v : part.vertices) {
        gldx::Vertex o = v;
        o.position = glm::vec3(xf * glm::vec4(v.position, 1.0f));
        o.normal   = glm::length(v.normal) > 0.0f
                         ? glm::normalize(nm * v.normal) : v.normal;
        o.color    = v.color * color;
        dst.vertices.push_back(o);
    }
    dst.indices.reserve(dst.indices.size() + part.indices.size());
    for (const std::uint32_t i : part.indices) dst.indices.push_back(base + i);
}

// One flat-shaded triangle, with the *normal* as the authority on its winding.
//
// Hand-winding a cylinder is where a faceted mesh loses half its surface to
// glCullFace(GL_BACK) - quietly, with every vertex still looking correct - so the
// order is derived here from the direction the face is meant to light. If the
// three points as given would put the geometric normal against the intended one,
// two of them swap. The cost is one cross product per triangle at build time; the
// benefit is that a wrong guess about "is theta clockwise in XZ" is not a bug a
// night render will show me.
void AddFlatTri(gldx::MeshData& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                const glm::vec3& facing, const glm::vec4& color) {
    glm::vec3 p0 = a, p1 = b, p2 = c;
    if (dot(cross(p1 - p0, p2 - p0), facing) < 0.0f) std::swap(p1, p2);
    const glm::vec3 n = glm::normalize(cross(p1 - p0, p2 - p0));
    const glm::vec2 uv(0.0f);   // foliage and bark are flat tones; no map is sampled
    const std::uint32_t i = static_cast<std::uint32_t>(d.vertices.size());
    d.vertices.push_back({p0, n, uv, {1.0f, 0.0f, 0.0f}, color});
    d.vertices.push_back({p1, n, uv, {1.0f, 0.0f, 0.0f}, color});
    d.vertices.push_back({p2, n, uv, {1.0f, 0.0f, 0.0f}, color});
    d.indices.push_back(i); d.indices.push_back(i + 1); d.indices.push_back(i + 2);
}

// A faceted cone with its base at `yBase`: `sides` triangles for the skirt and one
// cap. Used for the fir's three tiers; a cone is the whole reason the tree reads
// as a conifer at 60 metres and costs 8 triangles a tier to say so.
void AddCone(gldx::MeshData& d, float radius, float height, int sides, const glm::vec4& color,
             float yBase) {
    const glm::vec3 apex(0.0f, yBase + height, 0.0f);
    for (int s = 0; s < sides; ++s) {
        const float t0 = 6.283185307179586f * static_cast<float>(s) / static_cast<float>(sides);
        const float t1 = 6.283185307179586f * static_cast<float>(s + 1) / static_cast<float>(sides);
        const glm::vec3 b0(radius * std::cos(t0), yBase, radius * std::sin(t0));
        const glm::vec3 b1(radius * std::cos(t1), yBase, radius * std::sin(t1));
        // Outward, and tilted up by the slope: the average of the two radial
        // directions lifted by the height, which is close enough for a flat facet
        // whose real normal is whatever the three points produce.
        const glm::vec3 face = glm::normalize(
            glm::vec3(std::cos(0.5f * (t0 + t1)), radius / height, std::sin(0.5f * (t0 + t1))));
        AddFlatTri(d, apex, b0, b1, face, color);
        // The cap matters more for a fir than it looks: the lowest tier is at knee
        // height, so from a camera at eye level the underside of the tier above it
        // is a real surface, and without a cap that surface is the sky.
        AddFlatTri(d, glm::vec3(0.0f, yBase, 0.0f), b0, b1,
                   glm::vec3(0.0f, -1.0f, 0.0f), color);
    }
}

// A tapered faceted cylinder from `yBase` to `yBase + height`, sides only (no
// caps): tree trunk and lantern post, both of which are hidden at both ends.
void AddTube(gldx::MeshData& d, float rBottom, float rTop, float height, int sides,
             const glm::vec4& color, float yBase) {
    for (int s = 0; s < sides; ++s) {
        const float t0 = 6.283185307179586f * static_cast<float>(s) / static_cast<float>(sides);
        const float t1 = 6.283185307179586f * static_cast<float>(s + 1) / static_cast<float>(sides);
        const float m  = 0.5f * (t0 + t1);
        const glm::vec3 radial(std::cos(m), 0.0f, std::sin(m));
        // The outward tilt a taper deserves: with rTop < rBottom the facet leans
        // in, and a purely radial normal lights the trunk one band too bright at
        // the top of every facet.
        const float slope = (rBottom - rTop) / std::max(height, 1e-3f);
        const glm::vec3 face = glm::normalize(radial + slope * glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 b0(rBottom * std::cos(t0), yBase, rBottom * std::sin(t0));
        const glm::vec3 b1(rBottom * std::cos(t1), yBase, rBottom * std::sin(t1));
        const glm::vec3 up0(rTop * std::cos(t0), yBase + height, rTop * std::sin(t0));
        const glm::vec3 up1(rTop * std::cos(t1), yBase + height, rTop * std::sin(t1));
        AddFlatTri(d, b0, b1, up1, face, color);
        AddFlatTri(d, b0, up1, up0, face, color);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------
gldx::MeshData MakeFirMesh() {
    gldx::MeshData out;
    out.vertices.reserve(160);
    out.indices.reserve(160);

    // The alpha channel is the per-vertex material mask the fragment stage reads
    // as `vColor.a`: emissive strength for the lantern core, environment mix for
    // the crate, and 0 for everything that should never mirror or glow. Foliage
    // and bark are 0 - a pine does not reflect the moon.
    const glm::vec4 bark(0.42f, 0.30f, 0.20f, 0.0f);
    const glm::vec4 needle(0.16f, 0.34f, 0.24f, 0.0f);
    const glm::vec4 needleTip(0.20f, 0.40f, 0.28f, 0.0f);

    // Three tiers over one trunk, in the tree's own metres. Each tier starts below
    // the top of the one before it, which is what makes the silhouette a conifer
    // rather than a stack of separate hats, and the whole thing is one draw at any
    // population because the tiers are geometry, not objects.
    AddTube (out, 0.15f, 0.09f, 1.35f, 6, bark,       0.00f);
    AddCone (out, 0.95f, 1.55f, 8,     needle,        0.85f);
    AddCone (out, 0.72f, 1.40f, 8,     needle,        1.75f);
    AddCone (out, 0.45f, 1.20f, 8,     needleTip,     2.65f);
    return out;
}

gldx::MeshData MakeBroadleafMesh() {
    gldx::MeshData out;
    const glm::vec4 bark(0.55f, 0.44f, 0.32f, 0.0f);      // birch-pale
    const glm::vec4 leaf(0.26f, 0.40f, 0.18f, 0.0f);

    AddTube(out, 0.12f, 0.08f, 1.70f, 6, bark, 0.0f);
    // The loaf: a unit sphere squashed in Y and lifted. Non-uniform, so it is
    // baked here rather than handed to the instance (see the class comment).
    gldx::MeshData canopy = gldx::GeometryFactory::Sphere(1.0f, 12, 8);
    AddPart(out, canopy,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 2.15f, 0.0f))
          * glm::scale(glm::mat4(1.0f), glm::vec3(1.30f, 0.85f, 1.30f)),
            leaf);
    return out;
}

gldx::MeshData MakeCrateMesh() {
    // Cube(1) already gives one full 0..1 uv per face, which is what a plank panel
    // wants; alpha 1 lets the fragment stage spend part of this surface on the
    // captured cube. The tint stays near white so the map, not a colour multiply,
    // decides what wood this is.
    gldx::MeshData out = gldx::GeometryFactory::Cube(1.0f);
    for (gldx::Vertex& v : out.vertices) v.color = glm::vec4(0.98f, 0.92f, 0.82f, 1.0f);
    return out;
}

gldx::MeshData MakeLanternMesh() {
    gldx::MeshData out;
    const glm::vec4 iron(0.16f, 0.16f, 0.19f, 0.0f);
    const glm::vec4 flame(1.00f, 0.72f, 0.34f, 1.0f);

    AddTube(out, 0.055f, 0.045f, 1.55f, 5, iron, 0.0f);
    gldx::MeshData head = gldx::GeometryFactory::Cube(0.34f);
    AddPart(out, head,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.72f, 0.0f)), flame);
    return out;
}

gldx::MeshData MakeGroundMesh() {
    return gldx::GeometryFactory::Plane(1.0f);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------
gldx::Texture2DDesc MakeWhiteDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1;
    d.channels = 4;
    d.srgb = false;
    d.pixels = {255, 255, 255, 255};
    return d;
}

namespace {

// A 32-bit integer hash, used for both the speckle decisions in the maps and the
// periodic lattice below.
std::uint32_t Hash2(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 0x1f1f1f1fu);
    h = (h << 11) ^ (h >> 5) ^ static_cast<std::uint32_t>(y) * 0x27d4eb2du;
    h += h * 3u; h ^= h >> 16;
    h *= 0x85ebca6bu; h ^= h >> 13;
    return h;
}

float U01_2(std::int32_t x, std::int32_t y, std::uint32_t seed) {
    return static_cast<float>(Hash2(x, y, seed) % 100000u) / 100000.0f;
}

float Smooth01(float t) { return t * t * (3.0f - 2.0f * t); }

// Value noise that repeats with period 1 in both axes.
//
// The wrap is not cosmetic: Texture2D::Upload sets GL_REPEAT, so a map whose left
// edge disagrees with its right edge draws a visible grid across the ground every
// 48 tiles. Wrapping the lattice indices (not the output) makes the two edges
// sample the same cells, which is the cheapest way to be seamless.
float PNoise(float u, float v, int freq, std::uint32_t seed) {
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

float PFbm(float u, float v, int freq, std::uint32_t seed, int octaves) {
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

void Put(std::vector<std::uint8_t>& px, int size, int i, int j, const glm::vec3& c) {
    const float l = std::clamp(c.x, 0.0f, 1.0f), m = std::clamp(c.y, 0.0f, 1.0f),
                  n = std::clamp(c.z, 0.0f, 1.0f);
    const std::size_t o = (static_cast<std::size_t>(j) * size + i) * 4;
    px[o + 0] = static_cast<std::uint8_t>(l * 255.0f + 0.5f);
    px[o + 1] = static_cast<std::uint8_t>(m * 255.0f + 0.5f);
    px[o + 2] = static_cast<std::uint8_t>(n * 255.0f + 0.5f);
    px[o + 3] = 255;
}

} // namespace

gldx::Texture2DDesc MakeGrassDesc(int size, std::uint32_t seed) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 4;
    // Linear, not sRGB. The numbers below are written as the albedo the fragment
    // stage multiplies, so the upload must not decode them a second time: with
    // sRGB storage 0.115 arrives in the shader as 0.011 and the forest floor is a
    // decade darker than every flat tone in the scene that came straight from a
    // Vertex::color. Photographed maps want the opposite setting.
    d.srgb = false;
    d.pixels.assign(static_cast<std::size_t>(size) * size * 4, 0);

    // A daylight albedo, deliberately: the Gooch band dims it by a factor of four
    // or five at moonlight, so a map authored "night green" would be multiplied
    // twice and the floor would come out black.
    const glm::vec3 mossDark(0.115f, 0.170f, 0.100f);
    const glm::vec3 mossDry(0.290f, 0.265f, 0.140f);
    const glm::vec3 litter(0.330f, 0.215f, 0.105f);

    for (int j = 0; j < size; ++j) {
        const float v = static_cast<float>(j) / static_cast<float>(size);
        for (int i = 0; i < size; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(size);
            const float patch = PFbm(u, v, 4, seed, 3);           // clumps
            const float fine  = PFbm(u, v, 64, seed ^ 0x51edu, 2);
            const float tufts = PFbm(u, v, 18, seed ^ 0x27d4u, 2);
            glm::vec3 c = glm::mix(mossDark, mossDry, patch);
            c = glm::mix(c, litter, std::max(0.0f, tufts - 0.62f) * 2.1f);
            c *= 0.80f + 0.45f * fine;
            // Individual blades: a speckle at texel resolution, which is what
            // keeps the 48x tiling from turning into a smooth olive wash at the
            // far end of the field.
            if (Hash2(i, j, seed ^ 0x9e37u) % 11u == 0u) c *= 1.35f;
            Put(d.pixels, size, i, j, c);
        }
    }
    return d;
}

gldx::Texture2DDesc MakeWoodDesc(int size) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 4;
    d.srgb = false;   // linear, for the reason given on MakeGrassDesc
    d.pixels.assign(static_cast<std::size_t>(size) * size * 4, 0);

    const std::uint32_t seed = 0x85ebca6bu;
    for (int j = 0; j < size; ++j) {
        const float v = static_cast<float>(j) / static_cast<float>(size);
        for (int i = 0; i < size; ++i) {
            const float u = static_cast<float>(i) / static_cast<float>(size);
            // Grain: stretched along u, so the fibres run the length of a plank
            // instead of ringing the crate. An anisotropic lattice is also what
            // makes the map pass the "does this read as wood at 32 pixels" test.
            float g = PFbm(u * 0.25f, v, 24, seed, 3);
            g = g * 0.65f + 0.35f * PFbm(u * 0.25f, v, 96, seed ^ 0x1234u, 2);
            glm::vec3 c = glm::mix(glm::vec3(0.150f, 0.086f, 0.042f),
                                   glm::vec3(0.330f, 0.200f, 0.104f), g);
            // Four planks per face, with the gap darkened and the edge of each
            // plank catching a little more light than its middle.
            const float planks = 4.0f;
            const float band = v * planks;
            const float frac = band - std::floor(band);
            const float seam = 1.0f - std::exp(-std::abs(frac - 0.5f) * 26.0f);
            c *= 0.45f + 0.55f * seam;
            c *= 0.94f + 0.10f * std::sin(frac * 3.14159265f);
            Put(d.pixels, size, i, j, c);
        }
    }
    return d;
}

// ---------------------------------------------------------------------------
// The wander table
// ---------------------------------------------------------------------------
std::vector<Pose> MakeRoamTable(const Wander& w, std::uint32_t seed) {
    const int steps = static_cast<int>(w.horizon / std::max(w.stepSeconds, 1e-3)) + 1;
    std::vector<Pose> table;
    table.reserve(static_cast<std::size_t>(steps));

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> full(0.0f, 1.0f);
    std::uniform_real_distribution<float>Signed(-1.0f, 1.0f);

    const float half = 0.5f * w.size;
    glm::vec3 pos(0.0f, half, 0.0f);
    // The opening heading is random, which is the whole of "at a certain moment it
    // starts off in a random direction": nothing in the render loop picks a
    // direction, this table did, and it did it from the seed.
    float heading = full(rng) * 6.283185307179586f;

    for (int s = 0; s < steps; ++s) {
        const glm::vec3 dir(std::cos(heading), 0.0f, std::sin(heading));
        // yaw is stored as the angle that points the mesh's local -Z along `dir`,
        // which is the one convention that makes the crate lead with the face it
        // is travelling towards instead of sliding sideways.
        table.push_back({pos, std::atan2(dir.x, -dir.z)});

        if (full(rng) < w.turnChance)
            heading += Signed(rng) * w.turnMax;

        pos += dir * (w.speed * static_cast<float>(w.stepSeconds));
        const float d = std::sqrt(pos.x * pos.x + pos.z * pos.z);
        if (d > w.radius) {
            // Steer home rather than bounce: a reflection would send the crate
            // back along a mirror line, and a crate that paces one diameter back
            // and forth is a far less useful reflection test than one that crosses
            // the clearing at new angles every time.
            heading = std::atan2(-pos.x, -pos.z) + Signed(rng) * 0.35f;
            const float k = w.radius / std::max(d, 1e-4f);
            pos.x *= k; pos.z *= k;
        }
    }
    return table;
}

namespace {
float ShortestAngleLerp(float a, float b, float t) {
    constexpr float kTau = 6.283185307179586f;
    float d = std::fmod(b - a + kTau, kTau);
    if (d > 3.14159265f) d -= kTau;
    return a + d * t;
}
} // namespace

Pose SampleRoam(const std::vector<Pose>& table, const Wander& w, double seconds) {
    if (table.empty()) return {};
    const double local = seconds - w.startAt;
    if (local <= 0.0) return table.front();       // parked until its moment
    const double idx = local / std::max(w.stepSeconds, 1e-3);
    if (idx >= static_cast<double>(table.size() - 1)) return table.back();
    const std::size_t i0 = static_cast<std::size_t>(idx);
    const float f = static_cast<float>(idx - static_cast<double>(i0));
    const Pose& a = table[i0];
    const Pose& b = table[i0 + 1];
    return {glm::mix(a.pos, b.pos, f), ShortestAngleLerp(a.yaw, b.yaw, f)};
}

glm::mat4 CrateTransform(const Pose& p, float size) {
    return glm::translate(glm::mat4(1.0f), p.pos)
         * glm::rotate(glm::mat4(1.0f), p.yaw, glm::vec3(0.0f, 1.0f, 0.0f))
         * glm::scale(glm::mat4(1.0f), glm::vec3(size));
}

// ---------------------------------------------------------------------------
// Scene
// ---------------------------------------------------------------------------
namespace {

// Coarse occupancy grid for the scatter's minimum spacing. Checked against the
// nine cells around a candidate, so a full distance test against every tree
// already planted becomes a handful of them and 1400 trees build in milliseconds.
struct Spacing {
    float cell;
    int n;
    std::vector<std::vector<int>> buckets;
    std::vector<glm::vec2> pts;

    explicit Spacing(float radius, float spacing)
        : cell(spacing), n(static_cast<int>(2.0f * radius / spacing) + 2) {
        buckets.resize(static_cast<std::size_t>(n) * n);
    }
    int ix(float x) const { return std::clamp(static_cast<int>(x / cell) + n / 2, 0, n - 1); }
    bool tooClose(float x, float z, float minDist) const {
        const int cx = ix(x), cz = ix(z);
        const float d2 = minDist * minDist;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int gx = cx + dx, gz = cz + dz;
                if (gx < 0 || gz < 0 || gx >= n || gz >= n) continue;
                for (int p : buckets[static_cast<std::size_t>(gz) * n + gx]) {
                    const glm::vec2 q = pts[static_cast<std::size_t>(p)];
                    // dot of the difference with itself: `length2()` would do, but
                    // it lives in gtx and this project stays on core + gtc.
                    const glm::vec2 d = q - glm::vec2(x, z);
                    if (glm::dot(d, d) < d2) return true;
                }
            }
        return false;
    }
    void add(float x, float z) {
        pts.push_back(glm::vec2(x, z));
        const int i = static_cast<int>(pts.size()) - 1;
        buckets[static_cast<std::size_t>(ix(z)) * n + ix(x)].push_back(i);
    }
};

} // namespace

void Scene::Build() {
    const Config& c = config;
    gldx::RenderContext::AssertRenderThread("Scene::Build");

    white_.Upload(MakeWhiteDesc());
    grass_.Upload(MakeGrassDesc(512, c.seed));
    wood_.Upload(MakeWoodDesc(512));

    // ---- ground: one instance, sized by the matrix, tiled by the uv scale ----
    {
        gldx::Instance g;
        g.model = glm::scale(glm::mat4(1.0f), glm::vec3(c.groundHalf, 1.0f, c.groundHalf));
        g.color = glm::vec4(1.0f);
        ground_.Create(MakeGroundMesh(), {g});
    }

    // ---- the forest ----------------------------------------------------------
    std::vector<gldx::Instance> firs, broads;
    firs.reserve(static_cast<std::size_t>(c.treeCount));
    broads.reserve(static_cast<std::size_t>(c.treeCount));

    std::mt19937 rng(c.seed);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::uniform_real_distribution<float>Signed(-1.0f, 1.0f);
    Spacing spacing(c.ringMax, std::max(c.minSpacing, 0.5f));

    const float rMin = std::max(c.clearing, c.ringMin);
    int rejected = 0, attempts = 0;
    const int maxAttempts = c.treeCount * 8 + 64;
    // Area-uniform sampling: a uniform radius would pack the ring's inner edge
    // into a hedge and leave the outer edge bare, and a forest that thickens
    // toward the camera is the opposite of what a horizon line wants.
    const float aMin = rMin * rMin, aMax = c.ringMax * c.ringMax;

    while (static_cast<int>(firs.size() + broads.size()) < c.treeCount && attempts < maxAttempts) {
        ++attempts;
        const float ang = uni(rng) * 6.283185307179586f;
        const float r = std::sqrt(aMin + (aMax - aMin) * uni(rng));
        const float x = r * std::cos(ang), z = r * std::sin(ang);
        if (spacing.tooClose(x, z, c.minSpacing)) { ++rejected; continue; }
        spacing.add(x, z);

        gldx::Instance inst;
        const float scale = c.scaleMin + (c.scaleMax - c.scaleMin) * std::pow(uni(rng), 1.6f);
        // pow() skewed toward the small end: a field of equal-height trees reads as
        // a plantation, and the size spread is what a forest's silhouette actually
        // is. It is also the reason the instance scale is the second thing this
        // demo is named for after the offset.
        inst.model = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, z))
                   * glm::rotate(glm::mat4(1.0f), uni(rng) * 6.283185307179586f,
                                 glm::vec3(0.0f, 1.0f, 0.0f))
                   * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
        // Per-instance tint, multiplied with the vertex colour rather than
        // replacing it: this decides how pale this particular tree is, the mesh
        // decides which part of it is bark.
        const float j = 0.88f + 0.24f * uni(rng);
        if (uni(rng) * 100.0f < static_cast<float>(c.firShare)) {
            inst.color = glm::vec4(j, j * 1.02f, j * 1.06f, 1.0f);   // cool firs
            firs.push_back(inst);
        } else {
            inst.color = glm::vec4(j * 1.08f, j, j * 0.86f, 1.0f);   // warm broadleaf
            broads.push_back(inst);
        }
    }
    rejected_ = static_cast<std::uint32_t>(rejected);

    fir_.Create(MakeFirMesh(), std::move(firs));
    broadleaf_.Create(MakeBroadleafMesh(), std::move(broads));

    // ---- lanterns: four posts, and the same four positions become lights ------
    lanternPos_.clear();
    {
        std::vector<gldx::Instance> lamps;
        for (int i = 0; i < 4; ++i) {
            const float ang = 0.785398163f + static_cast<float>(i) * 1.570796327f
                            + Signed(rng) * 0.18f;
            const float rad = rMin + 1.4f + uni(rng) * 2.0f;
            const glm::vec3 p(rad * std::cos(ang), 0.0f, rad * std::sin(ang));
            lanternPos_.push_back(p + glm::vec3(0.0f, 1.72f, 0.0f));
            gldx::Instance inst;
            inst.model = glm::translate(glm::mat4(1.0f), p)
                       * glm::rotate(glm::mat4(1.0f), ang, glm::vec3(0.0f, 1.0f, 0.0f));
            inst.color = glm::vec4(1.0f, 0.96f + 0.05f * uni(rng), 0.90f, 1.0f);
            lamps.push_back(inst);
        }
        lantern_.Create(MakeLanternMesh(), std::move(lamps));
    }

    // ---- the crate, at the head of its table --------------------------------
    roam_ = MakeRoamTable(c.wander, c.seed ^ 0x5bf03635u);
    cratePose_ = SampleRoam(roam_, c.wander, 0.0);
    crate_.Create(MakeCrateMesh(),
                  {gldx::Instance{CrateTransform(cratePose_, c.wander.size), glm::vec4(1.0f)}});

    std::printf("night_forest: %u firs + %u broadleafs planted, %d candidates rejected"
                " (%d draws for the whole world)\n",
                fir_.count(), broadleaf_.count(), rejected, 5);
}

void Scene::UpdateCrate(double seconds, bool roaming) {
    gldx::RenderContext::AssertRenderThread("Scene::UpdateCrate");
    if (!crate_.valid()) return;
    cratePose_ = roaming ? SampleRoam(roam_, config.wander, seconds)
                        : (roam_.empty() ? Pose{} : roam_.front());
    gldx::Instance inst{CrateTransform(cratePose_, config.wander.size), glm::vec4(1.0f)};
    crate_.SetInstances({inst});
}

} // namespace night_forest
