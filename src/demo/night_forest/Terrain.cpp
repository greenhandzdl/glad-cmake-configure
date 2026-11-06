// night_forest demo — the height field: the manual's CTerrain rebuilt for a
// programmable pipeline, its three height sources, and the mesh it generates
// (see Terrain.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Terrain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

#include <glm/glm.hpp>

#include "Noise.h"

namespace night_forest {
namespace {

// The grid is uploaded as one VBO, so the vertex count is the thing a user can
// turn into an out-of-memory with a command-line typo: 1024 cells is already
// 1 050 625 vertices and 6.3 million indices, which is more terrain than a 400 m
// field at 2 m per cell needs by a factor of twenty-five.
constexpr std::uint32_t kMaxCells = 1024u;

// The manual's `MAP_SCALE`, in metres: the largest answerable cell size. Anything
// coarser and the field stops being a height map and becomes a few large tiles
// whose edges the fog cannot hide.
constexpr float kMaxCellMeters = 50.0f;

// How far the source grid may be resampled. A height map is data, not scenery, so
// a 65 536 x 65 536 `.raw` must be refused rather than streamed.
constexpr int kMaxSourceSide = 8192;

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

const char* TerrainModeName(TerrainMode m) {
    switch (m) {
        case TerrainMode::random:  return "random (3.1)";
        case TerrainMode::hills:   return "hills (3.1 smoothed)";
        case TerrainMode::contour: return "contour (3.2)";
        case TerrainMode::flat:    break;
    }
    return "flat";
}

bool ParseTerrainMode(const std::string& s, TerrainMode& out) {
    if (s == "flat")    { out = TerrainMode::flat;    return true; }
    if (s == "random")  { out = TerrainMode::random;  return true; }
    if (s == "hills")   { out = TerrainMode::hills;   return true; }
    if (s == "contour") { out = TerrainMode::contour; return true; }
    return false;
}

void Terrain::SetSize(std::uint32_t cells, float cellWidth) {
    cells_ = std::clamp(cells, 2u, kMaxCells);
    cell_  = std::clamp(cellWidth, 0.1f, kMaxCellMeters);
}

// ---------------------------------------------------------------------------
// The manual's 3.1: one random height per vertex
// ---------------------------------------------------------------------------
//
// The sample writes `g_terrain[Vertex][1] = h + FRAND * h` inside a double loop
// over `MAP_W * MAP_W`, so each vertex is an independent draw and adjacent
// vertices have no relationship at all. Two consequences of doing it exactly that
// way are worth stating, because the manual mentions neither and both show up in
// the screenshot:
//
//   - the surface is white noise, so its slope between two vertices is the full
//     height difference over one cell - with relief 40 m and a 2 m cell that is
//     80 degrees on average, and every vertex normal computed from it points
//     somewhere different. The Gooch band then reads as salt, not as hills. This
//     is why `hills` exists and why `--terrain-smooth` is offered for `random`.
//   - the heights are all positive (`h..2h`), which means the sheet does not sit
//     on y = 0 but floats h..2h above it. In the manual that is harmless because
//     nothing else in its scene knows where the ground is. Here the trees, the
//     lanterns, the crate and the walking camera are all placed *on* the surface,
//     so every mode is normalised to +-relief/2 - the same peak-to-peak amplitude
//     the manual's `h` asks for, at the reference plane everything else shares.
//
// `FRAND` becomes a hash of the vertex's lattice coordinates rather than a draw
// from a shared RNG stream. That is not laziness: an mt19937 stream advances once
// per vertex, so the height of vertex N would depend on how many vertices came
// before it, and changing MAP_W would move every mountain. A per-coordinate hash
// keeps the field reproducible under a grid change, which the demo's screenshot
// contract requires (and which is also why the tree scatter in Scene.cpp rejects
// candidates from a stream but only ever *samples* positions, never heights).
bool Terrain::InitTerrain(const TerrainDesc& desc, std::string& err) {
    SetSize(desc.cells, desc.cell);
    mode_       = desc.mode;
    relief_     = std::max(desc.relief, 0.0f);
    levels_     = desc.levels;
    smoothing_  = std::clamp(desc.smoothing, 0, 12);
    seed_       = desc.seed;
    source_     = "procedural";

    const std::size_t n = static_cast<std::size_t>(cells_ + 1u) * (cells_ + 1u);
    h_.assign(n, 0.0f);

    if (mode_ == TerrainMode::flat) {
        RecomputeBounds();
        return true;
    }

    const float half = 0.5f * static_cast<float>(cells_);
    const bool fromFbm = (mode_ == TerrainMode::hills)
                      || (mode_ == TerrainMode::contour && desc.heightMap.empty());
    if (mode_ == TerrainMode::random) {
        for (std::uint32_t j = 0; j <= cells_; ++j) {
            for (std::uint32_t i = 0; i <= cells_; ++i) {
                const float u = noise::U01_2(static_cast<std::int32_t>(i),
                                             static_cast<std::int32_t>(j), seed_);
                h_[at(i, j)] = u;
            }
        }
    } else if (fromFbm) {
        // `hills` and the file-less `contour` both want a continuous field, so
        // both start from the same fBm. The wavelength is a fraction of the span
        // rather than a fixed number of metres: 400 m of ground should hold the
        // same *five or six* ridges whether it is cut into 40 cells or 400, and a
        // user who halves `--terrain-cell` is asking for detail, not for a field
        // whose mountains multiply.
        const float wave = std::max(2.0f * half * cell_ / 6.0f, cell_);
        for (std::uint32_t j = 0; j <= cells_; ++j) {
            const float z = (static_cast<float>(j) - half) * cell_;
            for (std::uint32_t i = 0; i <= cells_; ++i) {
                const float x = (static_cast<float>(i) - half) * cell_;
                const float f = noise::Fbm2D(x, z, wave, seed_, 5);
                h_[at(i, j)] = f;
            }
        }
        source_ = "procedural fBm";
    }

    // 3.2: replace the field with the file's, when there is one. The two loaders
    // fill the same lattice, in the same metres, so nothing downstream has to
    // know which source produced a height.
    if (mode_ == TerrainMode::contour && !desc.heightMap.empty()) {
        std::string rawErr, imgErr;
        if (!LoadRawFile(desc.heightMap, rawErr) && !LoadImageFile(desc.heightMap, imgErr)) {
            // Both failed. Leave the field flat rather than half-loaded: a mesh
            // whose heights came from a truncated read is a mountain range with a
            // cliff where the file ran out, and that reads as a geometry bug. The
            // two loaders are tried in order because a `.raw` is not always named
            // `.raw`, and the reason each one refused is what the user needs.
            h_.assign(n, 0.0f);
            RecomputeBounds();
            err = rawErr + " (as an image: " + imgErr + ")";
            return false;
        }
    }

    if (smoothing_ > 0) Smooth(smoothing_);
    // Last arithmetic before the metres: every mode now holds a 0..1 shape, and
    // this is where it becomes a field of `relief` metres peak to peak. Done once,
    // here, rather than inside each producer, so the four sources cannot drift into
    // four different meanings of the same command-line number - and so a file whose
    // own datum is unknown (which is every contour picture) still fills the relief
    // it was asked for. Smoothing runs first because it is a shape operation: box
    // filtering a scaled field and scaling a filtered one differ only by rounding.
    Normalise();
    if (levels_ > 0) Quantize(levels_);
    RecomputeBounds();
    return true;
}

// ---------------------------------------------------------------------------
// Manual: bool loadRawFile(const char*)
// ---------------------------------------------------------------------------
// A `.raw` height map is a headerless grid of 16-bit values, which means the file
// carries no size - the manual's class knows the width from `m_nWidth` and reads
// `m_nWidth * m_nWidth` samples, so a file cut for a different grid silently reads
// past its end. Here the byte count decides: the side is sqrt(bytes / 2), and if
// that does not match the lattice the source is resampled instead of trusted.
bool Terrain::LoadRawFile(const std::string& path, std::string& err) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { err = "cannot open " + path; return false; }
    const std::streamoff bytes = f.tellg();
    f.seekg(0, std::ios::beg);
    if (bytes < 8) { err = path + " is too small to be a 16-bit grid"; return false; }
    if (bytes % 2 != 0) { err = path + " has an odd byte count, so no whole 16-bit grid fits"; return false; }

    const std::size_t count = static_cast<std::size_t>(bytes) / 2u;
    const int side = static_cast<int>(std::sqrt(static_cast<double>(count)) + 0.5);
    if (static_cast<std::size_t>(side) * static_cast<std::size_t>(side) != count) {
        err = path + " holds " + std::to_string(count) + " samples, which is not a square grid";
        return false;
    }
    if (side > kMaxSourceSide) {
        err = path + " is " + std::to_string(side) + "^2, above the "
              + std::to_string(kMaxSourceSide) + "^2 limit";
        return false;
    }

    std::vector<std::uint16_t> src(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    // Read into bytes so the file's little-endian layout survives on a big-endian
    // host too; every platform this demo ships on is little-endian anyway, and the
    // alternative (per-sample >>8 | <<8) would silently re-export a big-endian
    // export as a scrambled one.
    if (!f.read(reinterpret_cast<char*>(src.data()),
                static_cast<std::streamsize>(src.size() * sizeof(std::uint16_t)))) {
        err = path + " read failed at " + std::to_string(static_cast<std::size_t>(bytes)) + " bytes";
        return false;
    }

    const float smax = 1.0f / 65535.0f;
    const float scale = static_cast<float>(side - 1) / static_cast<float>(cells_);
    for (std::uint32_t j = 0; j <= cells_; ++j) {
        const float sy = std::clamp(static_cast<float>(j) * scale, 0.0f,
                                    static_cast<float>(side - 1));
        const int y0 = static_cast<int>(sy);
        const float ty = sy - static_cast<float>(y0);
        const int y1 = std::min(y0 + 1, side - 1);
        for (std::uint32_t i = 0; i <= cells_; ++i) {
            const float sx = std::clamp(static_cast<float>(i) * scale, 0.0f,
                                        static_cast<float>(side - 1));
            const int x0 = static_cast<int>(sx);
            const float tx = sx - static_cast<float>(x0);
            const int x1 = std::min(x0 + 1, side - 1);
            const auto s = [&](int x, int y) {
                return static_cast<float>(src[static_cast<std::size_t>(y) * side + x]) * smax;
            };
            const float u = Lerp(Lerp(s(x0, y0), s(x1, y0), tx),
                                 Lerp(s(x0, y1), s(x1, y1), tx), ty);
            h_[at(i, j)] = u;
        }
    }
    source_ = "raw " + std::to_string(side) + "^2 16-bit: " + path;
    return true;
}

// ---------------------------------------------------------------------------
// Manual 3.2: contour map picture -> heights
// ---------------------------------------------------------------------------
// The sample reads `g_imageData[(z*MAP_W+x)*3] / 3` - the first byte of the x-th
// pixel of a BGR BMP, divided by three to land in some usable metre range. Decoding
// through stb instead of `auxDIBImageLoad` gives the same number (luma of the pixel)
// without the channel order, the 8/24-bit branch or the device context, and asking
// stb for one channel means the mix weights are stb's, not mine. The `/ 3` does not
// come across, because it divides a 0..255 byte by a constant chosen for one picture:
// the loader keeps the shape and leaves the metres to `relief`, which Normalise()
// applies to all four sources the same way.
//
// Row order: the picture's first row becomes the lattice's j = 0, which is world
// -Z. The manual's `-float(z)*MAP_SCALE` does the same thing for its own camera,
// which sat at +Z looking towards the origin; here the field has to agree with the
// cube map's -Z face (the panorama reprojection and the capture both use it), so
// the same convention survives for a different reason - worth knowing if a height
// map ever looks rotated by 180 degrees against the sky it was painted for.
bool Terrain::LoadImageFile(const std::string& path, std::string& err) {
    auto img = gldx::LoadImageToDesc(path, 1);
    if (!img) { err = img.error(); return false; }
    const int w = img->width, h = img->height;
    if (w < 2 || h < 2) { err = path + " is " + std::to_string(w) + "x" + std::to_string(h)
                                  + ", too small to sample a height from"; return false; }

    const float inv = 1.0f / 255.0f;
    const float sx = static_cast<float>(w - 1) / static_cast<float>(cells_);
    const float sy = static_cast<float>(h - 1) / static_cast<float>(cells_);
    const auto px = [&](int x, int y) {
        return static_cast<float>(img->pixels[static_cast<std::size_t>(y) * w + x]) * inv;
    };
    for (std::uint32_t j = 0; j <= cells_; ++j) {
        const float fy = std::clamp(static_cast<float>(j) * sy, 0.0f, static_cast<float>(h - 1));
        const int y0 = static_cast<int>(fy);
        const int y1 = std::min(y0 + 1, h - 1);
        const float ty = fy - static_cast<float>(y0);
        for (std::uint32_t i = 0; i <= cells_; ++i) {
            const float fx = std::clamp(static_cast<float>(i) * sx, 0.0f, static_cast<float>(w - 1));
            const int x0 = static_cast<int>(fx);
            const int x1 = std::min(x0 + 1, w - 1);
            const float tx = fx - static_cast<float>(x0);
            const float u = Lerp(Lerp(px(x0, y0), px(x1, y0), tx),
                                 Lerp(px(x0, y1), px(x1, y1), tx), ty);
            h_[at(i, j)] = u;
        }
    }
    source_ = "image " + std::to_string(w) + "x" + std::to_string(h) + ": " + path;
    return true;
}

// ---------------------------------------------------------------------------
// Manual: int getHeight(int x, int y) / float getAveHeight(float x, float z)
// ---------------------------------------------------------------------------
float Terrain::GetHeight(std::uint32_t ix, std::uint32_t iz) const {
    if (h_.empty()) return 0.0f;
    const std::uint32_t x = std::min(ix, cells_);
    const std::uint32_t z = std::min(iz, cells_);
    return h_[at(x, z)];
}

// The manual's name says "average height" and its body averages the four corners
// of the cell a world position falls in, weighted by how far each is - which is
// simply bilinear sampling, and is the reason a camera or a tree base can be
// placed off-lattice at all. Nearest-vertex would work too and would show its
// grid: the walking camera would step down a whole cell height every few metres.
float Terrain::GetAveHeight(float x, float z) const {
    if (h_.empty() || cell_ <= 0.0f) return 0.0f;
    const float half = 0.5f * static_cast<float>(cells_);
    const float fx = std::clamp(x / cell_ + half, 0.0f, static_cast<float>(cells_));
    const float fz = std::clamp(z / cell_ + half, 0.0f, static_cast<float>(cells_));
    // The last cell gets the whole interval to itself: at fx == cells_ the +1
    // corner does not exist, so the weight collapses onto the edge instead of
    // reading one vertex past the row.
    const std::uint32_t x0 = static_cast<std::uint32_t>(std::min(fx, static_cast<float>(cells_ - 1u)));
    const std::uint32_t z0 = static_cast<std::uint32_t>(std::min(fz, static_cast<float>(cells_ - 1u)));
    const float tx = fx - static_cast<float>(x0);
    const float tz = fz - static_cast<float>(z0);
    const float a = Lerp(GetHeight(x0, z0),     GetHeight(x0 + 1u, z0),     tx);
    const float b = Lerp(GetHeight(x0, z0 + 1u), GetHeight(x0 + 1u, z0 + 1u), tx);
    return Lerp(a, b, tz);
}

// ---------------------------------------------------------------------------
// Post-processing the lattice
// ---------------------------------------------------------------------------
namespace {
// One 3x3 box pass over the lattice, clamped at the border.
//
// Clamping (rather than wrapping, which is what Noise.h's periodic family does for
// a tiling texture) is correct here because the field has edges: wrapping would
// average the west column into the east one and pull the two borders towards each
// other into a shared ridge.
float BoxAt(const std::vector<float>& src, std::uint32_t cells, int i, int j) {
    const int ci = std::clamp(i, 0, static_cast<int>(cells));
    const int cj = std::clamp(j, 0, static_cast<int>(cells));
    return src[static_cast<std::size_t>(cj) * (cells + 1u) + static_cast<std::size_t>(ci)];
}
} // namespace

void Terrain::Normalise() {
    if (h_.empty() || relief_ <= 0.0f) return;
    RecomputeBounds();
    const float range = maxH_ - minH_;
    // A degenerate field (a uniform grey picture, a flat .raw) is put on the plane
    // rather than divided by its own zero range into an infinite slope. Any datum
    // would do, and 0 is the one the flat sheet uses, so a file that turns out to
    // carry no shape looks exactly like --terrain flat instead of like a crash.
    if (range <= 1e-6f) { h_.assign(h_.size(), 0.0f); return; }
    const float k = relief_ / range;
    const float mid = 0.5f * (maxH_ + minH_);
    for (float& v : h_) v = (v - mid) * k;
}

void Terrain::Smooth(int passes) {
    if (h_.empty() || passes <= 0) return;
    std::vector<float> tmp = h_;
    const std::uint32_t c = cells_;
    for (int p = 0; p < passes; ++p) {
        for (std::uint32_t j = 0; j <= c; ++j) {
            for (std::uint32_t i = 0; i <= c; ++i) {
                float sum = 0.0f;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di)
                        sum += BoxAt(h_, c, static_cast<int>(i) + di, static_cast<int>(j) + dj);
                tmp[at(i, j)] = sum / 9.0f;
            }
        }
        std::swap(h_, tmp);
    }
}

// The manual's 3.2 second half: it lifts whole windows of the grid by +40, +30,
// +20 and +10 in overlapping bands, which turns a smooth picture-derived field
// into terraces with flat tops and near-vertical skirts. Doing it as a quantiser
// rather than as hard-coded windows is the same operation with the arithmetic made
// explicit: the height range is cut into N steps and every vertex snaps to its
// step's centre, so overlapping "which window am I in" rules cannot disagree.
void Terrain::Quantize(int levels) {
    if (h_.empty()) return;
    const int n = std::clamp(levels, 2, 256);
    RecomputeBounds();
    const float range = maxH_ - minH_;
    if (range <= 1e-4f) return;
    const float step = range / static_cast<float>(n);
    for (float& v : h_) {
        // The band index is clamped, not just floored: `v == maxH_` divides by
        // `step` into exactly n, and letting that through produces a band n above
        // the n-1 legal ones, whose centre sits half a step above the field's own
        // ceiling. The terraces would then reach maxH_ + step/2 - outside the
        // +-relief/2 envelope that Normalise() just promised to every other mode,
        // which the HUD's min/max line reports as a field taller than its relief.
        // Clamped, every terrace lies inside the envelope and the terraced span is
        // relief - step, i.e. relief * (n-1) / n: the unavoidable cost of snapping
        // to band centres rather than to band floors.
        const float band = std::min(std::floor((v - minH_) / step),
                                    static_cast<float>(n - 1));
        const float k = band + 0.5f;
        v = minH_ + k * step;
    }
}

void Terrain::RecomputeBounds() {
    minH_ = 0.0f;
    maxH_ = 0.0f;
    if (h_.empty()) return;
    minH_ = h_[0];
    maxH_ = h_[0];
    for (const float v : h_) {
        minH_ = std::min(minH_, v);
        maxH_ = std::max(maxH_, v);
    }
}

// ---------------------------------------------------------------------------
// Manual: void render() - the geometry half, on the CPU
// ---------------------------------------------------------------------------
//
// The loop is the manual's, including `Vertex = z * MAP_W + x` and the integer
// texture coordinates, with two changes that are not cosmetic:
//
//   - It runs to `cells` inclusive, not `< cells`. The manual's own index idiom
//     (`g_index[index++] = Vertex; g_index[index++] = Vertex + MAP_W;`) reaches one
//     row past its `MAP_W * MAP_W` vertex array on the last iteration, so either
//     its array is one row bigger than the loop writes or its last row reads
//     uninitialised heights. The (MAP_W + 1)^2 lattice is what a MAP_W-cell grid
//     needs, and that is what is built here.
//   - The index stream is GL_TRIANGLES, six per cell, not the manual's two-per-
//     vertex triangle-strip list. InstancedMesh's draw is glDrawElementsInstanced
//     with a fixed GL_TRIANGLES mode, and a strip would need one strip per row
//     band plus a restart index, which instancing cannot express as a single
//     element count. The vertices and their layout are identical either way; only
//     the index count differs, and it is the honest cost of instancing.
gldx::MeshData Terrain::BuildMeshData() const {
    gldx::MeshData d;
    if (h_.empty()) return d;

    const std::size_t verts = static_cast<std::size_t>(cells_ + 1u) * (cells_ + 1u);
    d.vertices.resize(verts);
    d.indices.reserve(static_cast<std::size_t>(cells_) * cells_ * 6u);

    const float half = 0.5f * static_cast<float>(cells_);
    const float range = std::max(maxH_ - minH_, 1e-3f);

    for (std::uint32_t j = 0; j <= cells_; ++j) {
        for (std::uint32_t i = 0; i <= cells_; ++i) {
            const float h = h_[at(i, j)];
            const glm::vec3 p((static_cast<float>(i) - half) * cell_,
                              h,
                              (static_cast<float>(j) - half) * cell_);

            // The analytic normal of a height field: the surface is (x, f(x,z), z),
            // so its two tangents are (1, df/dx, 0) and (0, df/dz, 1), and the
            // flipped cross product is (-df/dx, 1, -df/dz). Central differences
            // across two cells, because a one-cell difference would make every
            // vertex's normal belong to one triangle only and show the grid as
            // faceting on a field that is already smoothed.
            const float dhx = (GetHeight(std::min(i + 1u, cells_), j)
                               - GetHeight(i > 0u ? i - 1u : 0u, j))
                              / (2.0f * cell_);
            const float dhz = (GetHeight(i, std::min(j + 1u, cells_))
                               - GetHeight(i, j > 0u ? j - 1u : 0u))
                              / (2.0f * cell_);
            const glm::vec3 n = glm::normalize(glm::vec3(-dhx, 1.0f, -dhz));

            // Two tints, both of which exist to keep a lit hillside readable rather
            // than uniformly moss-green: steep faces go to a cold grey rock, and
            // the tops of ridges dry out. The alpha is the environment mask the
            // fragment stage gates reflections on (Shaders.h) and it stays 0 - the
            // ground never mirrors the sky, whatever it is made of.
            const float steep = std::clamp((1.0f - n.y - 0.10f) * 3.5f, 0.0f, 1.0f);
            const float high  = std::clamp((h - minH_) / range, 0.0f, 1.0f);
            glm::vec3 tone = glm::mix(glm::vec3(1.0f), glm::vec3(0.62f, 0.60f, 0.57f), steep);
            tone = glm::mix(tone, tone * glm::vec3(1.10f, 1.06f, 0.92f), 0.55f * high * high);

            gldx::Vertex& v = d.vertices[at(i, j)];
            v.position = p;
            v.normal   = n;
            v.uv       = glm::vec2(static_cast<float>(i), static_cast<float>(j));
            v.tangent  = glm::vec3(1.0f, 0.0f, 0.0f);
            v.color    = glm::vec4(tone, 0.0f);
        }
    }

    for (std::uint32_t j = 0; j < cells_; ++j) {
        for (std::uint32_t i = 0; i < cells_; ++i) {
            const std::uint32_t v00 = static_cast<std::uint32_t>(at(i, j));
            const std::uint32_t v10 = static_cast<std::uint32_t>(at(i + 1u, j));
            const std::uint32_t v01 = static_cast<std::uint32_t>(at(i, j + 1u));
            const std::uint32_t v11 = static_cast<std::uint32_t>(at(i + 1u, j + 1u));
            // The same diagonal and the same order as GeometryFactory::Plane's
            // {0,2,1, 0,3,2}: with +X increasing with i and +Z with j, both
            // triangles' right-hand normal is +Y, so they are front-facing under
            // the renderer's GL_CCW + GL_BACK cull. Getting this backwards does not
            // show up on a hillside at all - it shows up as the whole field
            // vanishing whenever the camera is above it, which is the default.
            d.indices.push_back(v00); d.indices.push_back(v11); d.indices.push_back(v10);
            d.indices.push_back(v00); d.indices.push_back(v01); d.indices.push_back(v11);
        }
    }
    return d;
}

} // namespace night_forest
