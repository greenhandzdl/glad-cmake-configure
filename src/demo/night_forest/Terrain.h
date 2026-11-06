#ifndef NIGHT_FOREST_TERRAIN_H
#define NIGHT_FOREST_TERRAIN_H

/**
 * @file Terrain.h
 * @brief The height field: the experiment manual's `CTerrain` class rebuilt for a
 *        programmable pipeline, one member at a time.
 *
 * The manual's terrain class is a fixed-function object: it holds a `BYTE` height
 * map, a `CBMPLoader` texture, and a `render()` that both generates the grid and
 * issues the draw. Those two jobs cannot stay merged here, because this project's
 * rule is that CPU-side data (Stage A) and GL uploads (Stage B) are separate
 * phases - so `render()` splits into `BuildMeshData()` (pure CPU, worker-thread
 * safe) and whatever pass draws the result. Every other member keeps the manual's
 * meaning and, where the shape of the answer changed, the reason is in the comment
 * on that member rather than in a footnote.
 *
 * What the manual does *not* have, and what a height field needs in a lit scene,
 * is normals: the fixed-function sample sends no `glNormal3f` at all, so its
 * terrain is shaded by whatever the last normal happened to be. Here the normal is
 * the analytic one - the gradient of the height field - because the Gooch band
 * reads `dot(N, L)` and a wrong N turns a hillside into a stripe of the wrong flat
 * tone. That is also why the height storage is `float` metres and not `BYTE`: with
 * 8 bits the gradient of a 400 m field quantises to 1.5 m steps, and the band
 * would be computing a staircase.
 *
 * Three sources for the heights, matching the manual's three cases:
 *   random  - 3.1, `h + FRAND * h`, one independent value per vertex.
 *   hills   - 3.1's idea smoothed into ridges, for the "rolling hills" the
 *             manual's intro asks the camera to climb over.
 *   contour - 3.2, heights from a file: a 16-bit `.raw` (the manual's
 *             `loadRawFile`) or a grayscale picture, optionally terraced into
 *             bands like the manual's +40/+30/+20/+10 edits.
 *
 * Reproducibility: every procedural height here is a pure function of lattice
 * coordinates and `seed` (Noise.h), and the file paths are read once at build
 * time, so a terrain frame is as bit-reproducible as the grass map is. No RNG
 * stream is advanced per vertex, which is what would break if the vertex count
 * changed - and the vertex count is a user-facing knob.
 *
 * NOTE: names gldx engine types (MeshData), so include AFTER `import gldx;`.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace night_forest {

enum class TerrainMode {
    flat,     // no relief: Scene keeps the engine's single quad sheet
    random,   // manual 3.1: white noise per vertex
    hills,    // manual 3.1 smoothed into continuous ridges
    contour,  // manual 3.2: from a file, optionally terraced into bands
};

const char* TerrainModeName(TerrainMode m);
// Parses "flat" | "random" | "hills" | "contour"; false on anything else, so
// main.cpp can report the typo instead of quietly drawing the wrong experiment.
bool ParseTerrainMode(const std::string& s, TerrainMode& out);

struct TerrainDesc {
    TerrainMode mode  = TerrainMode::flat;

    // ---- the manual's setSize(width, cell) pair ----
    // `cells` is its MAP_W (grid count), `cell` its MAP_SCALE (metres per grid).
    // The grid is
    // (cells + 1) squared: see the comment in Terrain.cpp on why the manual's
    // loop bound and its `Vertex + MAP_W` index idiom cannot both be right.
    std::uint32_t cells = 200;
    float cell = 2.0f;

    // Metres, peak to peak: the field's lowest vertex is at -relief/2 and its
    // highest at +relief/2, whichever mode produced it. This is the manual's `h`
    // with the ambiguity removed - its sample writes `h + FRAND * h`, which spans
    // exactly h metres but above the y = 0 plane rather than around it, so `h` is
    // an amplitude there and a datum offset at the same time. Normalising to the
    // centre makes the number comparable across the four modes and across a file
    // whose own units nobody knows, which is what a contour picture always is.
    float relief = 40.0f;
    int   smoothing = 0;              // box-filter passes over the lattice
    int   levels = 0;                 // 0 = continuous; N = terrace into N bands
    std::string heightMap;            // .raw (16-bit) or a grayscale picture
    std::uint32_t seed = 20261104u;
};

class Terrain {
public:
    // ---- manual: void setSize(unsigned int width, unsigned int cell) ---------
    // Grid count and metres per grid. Allocates nothing; InitTerrain() does.
    void SetSize(std::uint32_t cells, float cellWidth);

    // ---- manual: bool initTerrain() -----------------------------------------
    // Allocates and fills the height lattice for `desc`. For the two procedural
    // modes this is pure arithmetic; for `contour` it reads the file named in
    // `desc.heightMap` through LoadRawFile / LoadImageFile first, and reports the
    // reason in `err` if that fails (returning false and leaving the terrain flat
    // rather than drawing a half-loaded field).
    bool InitTerrain(const TerrainDesc& desc, std::string& err);

    // ---- manual: bool loadRawFile(const char* fileName) ---------------------
    // A headerless grid of 16-bit heights. The side is inferred from the byte
    // count (square), so a file whose grid does not match `cells` resamples
    // instead of reading past the buffer - the manual's version assumes the two
    // agree and indexes `g_imageData` out of bounds when they do not.
    bool LoadRawFile(const std::string& path, std::string& err);

    // A grayscale or colour picture as heights: the manual's 3.2 reads the BMP's
    // first channel byte; here stb decodes, and luma becomes height.
    bool LoadImageFile(const std::string& path, std::string& err);

    // ---- manual: int getHeight(int x, int y) --------------------------------
    // One lattice vertex, clamped to the grid. Returns float metres, not the
    // manual's int, for the reason given in the class comment.
    float GetHeight(std::uint32_t ix, std::uint32_t iz) const;

    // ---- manual: float getAveHeight(float x, float z) ----------------------
    // World metres in, height out: the average of the four surrounding vertices,
    // weighted - which is bilinear sampling, and exactly what the manual's name
    // claims. Everything that has to sit on the ground asks this: the tree
    // scatter, the lanterns, the crate's wander table and the walking camera.
    float GetAveHeight(float x, float z) const;

    // ---- manual: void render() ---------------------------------------------
    // The generation half of it, CPU-only. Positions are already in world metres,
    // so the draw needs one identity instance; uv carries the manual's integer
    // lattice (`g_texcoord = (x, z)`), which is why the tiling density is a
    // uniform the pass sets rather than a number baked into the vertices.
    gldx::MeshData BuildMeshData() const;

    // ---- accessors the scene and the HUD need ------------------------------
    [[nodiscard]] bool reliefActive() const noexcept {
        return mode_ != TerrainMode::flat && cell_ > 0.0f;
    }
    [[nodiscard]] TerrainMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::uint32_t cells() const noexcept { return cells_; }
    [[nodiscard]] float cell() const noexcept { return cell_; }
    [[nodiscard]] float relief() const noexcept { return relief_; }
    [[nodiscard]] int levels() const noexcept { return levels_; }
    [[nodiscard]] int smoothing() const noexcept { return smoothing_; }
    // Half the span in metres: the grid covers [-half, +half] in X and Z.
    [[nodiscard]] float halfWidth() const noexcept {
        return 0.5f * static_cast<float>(cells_) * cell_;
    }
    [[nodiscard]] std::uint64_t vertexCount() const noexcept {
        return static_cast<std::uint64_t>(h_.size());
    }
    [[nodiscard]] std::uint64_t indexCount() const noexcept {
        return reliefActive() ? 6ULL * cells_ * cells_ : 0ULL;
    }
    // Lowest and highest vertex, for the HUD's "%+.1f..%+.1f m" line.
    [[nodiscard]] float minHeight() const noexcept { return minH_; }
    [[nodiscard]] float maxHeight() const noexcept { return maxH_; }
    // What the last Load* call actually produced, for the HUD's source line.
    [[nodiscard]] const std::string& source() const noexcept { return source_; }

private:
    [[nodiscard]] std::size_t at(std::uint32_t ix, std::uint32_t iz) const noexcept {
        return static_cast<std::size_t>(iz) * (cells_ + 1u) + ix;
    }
    void RecomputeBounds();
    void Normalise();
    void Smooth(int passes);
    void Quantize(int levels);

    std::uint32_t cells_ = 0;
    float  cell_   = 0.0f;
    float  relief_ = 0.0f;
    int    levels_ = 0;
    int    smoothing_ = 0;
    std::uint32_t seed_ = 0u;
    TerrainMode mode_ = TerrainMode::flat;
    std::vector<float> h_;            // the manual's m_pHeightMap, in metres
    float minH_ = 0.0f, maxH_ = 0.0f;
    std::string source_ = "procedural";
};

} // namespace night_forest

#endif // NIGHT_FOREST_TERRAIN_H
