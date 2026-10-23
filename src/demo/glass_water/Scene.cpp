// glass_water demo — scene data: tiled water grid, procedural maps, item table
// (see Scene.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Scene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace glass_water {

namespace {

// Colours in this file are authored as sRGB hues (0.6 "sky blue" reads the way
// it looks in a image viewer) but they are consumed by a fragment stage writing
// into the linear HDR target, so they get the same approximate-gamma decode the
// sRGB-tagged maps do. Authoring them straight in linear would make every tint
// in the scene look far more saturated than it was picked to be.
glm::vec3 ToLinear(const glm::vec3& srgb) {
    return {static_cast<float>(std::pow(srgb.r, 2.2)),
            static_cast<float>(std::pow(srgb.g, 2.2)),
            static_cast<float>(std::pow(srgb.b, 2.2))};
}

// A box-shaped model matrix: unit cube scaled to `size` then moved to `center`.
// Everything opaque, blended and glowing in the scene is either this or a
// scaled sphere, because one cube mesh scaled three ways is cheaper to reason
// about than a factory call per piece of furniture.
glm::mat4 Box(const glm::vec3& center, const glm::vec3& size) {
    return glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), size);
}

// A sphere-shaped model matrix (uniform scale, so the normal matrix is trivial).
glm::mat4 Ball(const glm::vec3& center, float radius) {
    return Box(center, glm::vec3(radius));
}

// One entry of a placement table: where, and how big. Used for every group of
// repeated objects below, so the geometry reads as data rather than as eight
// near-identical calls.
struct Placed {
    glm::vec3 center;
    glm::vec3 size;
};

// The sky's three bands as authored sRGB hues - the single place they are
// written down. Everything else about the sky derives from them: the dome's
// gradient map, and the linear radiance the shader reflects.
//
// The horizon is deliberately not the palest band in the ramp. A milky horizon is
// what made the first PBR frame read as one flat grey wash: a horizontal surface
// takes its ambient from exactly that direction (SkyRadiance(n) for n straight up
// still lands on the horizon band), so every value in the scene - the lake, the
// panes, the tiles - was being lifted by the same near-white term, and the
// Fresnel that is supposed to be the demo's subject had nothing to contrast
// against. A deep blue horizon costs one constant and gives the mirror band
// somewhere to be bright relative to.
const glm::vec3 kSkyZenithSrgb {0.11f, 0.25f, 0.48f};
const glm::vec3 kSkyHorizonSrgb{0.46f, 0.62f, 0.82f};
const glm::vec3 kSkyNadirSrgb  {0.07f, 0.08f, 0.11f};

} // namespace

const glm::vec3 kSkyZenith  = ToLinear(kSkyZenithSrgb);
const glm::vec3 kSkyHorizon = ToLinear(kSkyHorizonSrgb);
const glm::vec3 kSkyNadir   = ToLinear(kSkyNadirSrgb);

// HDR, and deliberately well above 1: the sun has to be able to blow out a
// specular glint on the water and still leave the diffuse basin under the ACES
// shoulder. A "white" light at 1.0 makes every material in the scene read as
// matte paper, no matter what its roughness says.
const glm::vec3 kSunColor{3.1f, 2.9f, 2.5f};

// ---- procedural maps --------------------------------------------------------

gldx::Texture2DDesc MakeWhiteDesc() {
    gldx::Texture2DDesc d;
    d.width = d.height = 1;
    d.channels = 4;
    d.srgb = false;   // a constant, not a photograph: nothing to decode
    d.pixels = {255, 255, 255, 255};
    return d;
}

gldx::Texture2DDesc MakeCheckerDesc(int size, int cells) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 3;   // no alpha: an opaque map, so the byte count drops a quarter
    d.srgb = true;
    d.pixels.assign(static_cast<std::size_t>(size) * size * 3, 0);
    const int cell = std::max(1, size / cells);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Two greys plus a thin dark seam: this is the test card the panes are
            // judged against, so it stays high contrast. The uvScales below lay it
            // out in world units (about one cell per unit) instead of picking a
            // per-object repeat count, which is what kept it from aliasing into
            // moiré on the long rims.
            const bool a = ((x / cell) + (y / cell)) % 2 == 0;
            const bool seam = x % cell < 2 || y % cell < 2;
            const int v = seam ? 68 : (a ? 186 : 132);
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 3;
            d.pixels[i] = static_cast<std::uint8_t>(v);
            d.pixels[i + 1] = static_cast<std::uint8_t>(v);
            d.pixels[i + 2] = static_cast<std::uint8_t>(v);
        }
    }
    return d;
}

gldx::Texture2DDesc MakeCutoutDesc(int size, int bars) {
    gldx::Texture2DDesc d;
    d.width = d.height = size;
    d.channels = 4;
    d.srgb = true;
    d.pixels.assign(static_cast<std::size_t>(size) * size * 4, 0);
    const int bar = std::max(1, size / bars);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Slats on both axes plus a solid frame: the open squares in between
            // are alpha 0, which is the only thing separating this from a plain
            // translucent sheet - a hole is not "transparent", it is "absent".
            const bool onBar = x % bar < bar * 0.42f || y % bar < bar * 0.42f;
            const bool frame = x < bar / 2 || y < bar / 2
                            || x >= size - bar / 2 || y >= size - bar / 2;
            const int v = onBar || frame ? 208 : 0;
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4;
            d.pixels[i] = d.pixels[i + 1] = d.pixels[i + 2] = static_cast<std::uint8_t>(v);
            d.pixels[i + 3] = onBar || frame ? 255 : 0;
        }
    }
    // Known, and worth knowing: Texture2D::Upload always builds a full mip chain,
    // so each level averages the slats with the holes and the openings close up
    // with distance - an alpha-tested fence fades toward opaque rather than
    // staying see-through. Fixing it needs coverage-aware mip filtering (or an
    // alpha-erode channel), which is a different lesson from the one this demo
    // teaches, so the artefact is left visible instead of quietly papered over.
    return d;
}

gldx::Texture2DDesc MakeSkyDesc(int height) {
    constexpr int kWidth = 8;   // the dome's gradient only varies with latitude
    gldx::Texture2DDesc d;
    d.width = kWidth;
    d.height = height;
    d.channels = 4;
    d.srgb = true;
    d.pixels.assign(static_cast<std::size_t>(kWidth) * height * 4, 0);
    // Zenith -> horizon -> nadir. The horizon band is deliberately the brightest
    // thing in the map: it is what a grazing reflection on the water and on the
    // panes picks up, and a flat sky makes every reflection look like a tint.
    // Same three bands, same linear ramp, as SkyRadiance() in Shaders.h - the
    // shader cannot reflect a sky the dome is not showing.
    const auto sample = [](float v) -> std::array<std::uint8_t, 3> {
        auto mix3 = [](const glm::vec3& a, const glm::vec3& b, float t) {
            const glm::vec3 m = a + (b - a) * t;
            return std::array<std::uint8_t, 3>{
                static_cast<std::uint8_t>(m.x * 255.0f),
                static_cast<std::uint8_t>(m.y * 255.0f),
                static_cast<std::uint8_t>(m.z * 255.0f)};
        };
        return v < 0.5f ? mix3(kSkyZenithSrgb, kSkyHorizonSrgb, v * 2.0f)
                        : mix3(kSkyHorizonSrgb, kSkyNadirSrgb, (v - 0.5f) * 2.0f);
    };
    for (int y = 0; y < height; ++y) {
        const auto rgb = sample(static_cast<float>(y) / (height - 1));
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kWidth + x) * 4;
            d.pixels[i] = rgb[0];
            d.pixels[i + 1] = rgb[1];
            d.pixels[i + 2] = rgb[2];
            d.pixels[i + 3] = 255;
        }
    }
    return d;
}

// ---- the tiled water sheet ---------------------------------------------------

gldx::MeshData MakeWaterGrid(float hx, float hz, int tilesX, int tilesZ,
                             int cellsPerTile, std::vector<Tile>& outTiles) {
    tilesX = std::max(1, tilesX);
    tilesZ = std::max(1, tilesZ);
    cellsPerTile = std::max(1, cellsPerTile);
    const int gridX = tilesX * cellsPerTile;   // cells across the whole sheet
    const int gridZ = tilesZ * cellsPerTile;
    const int vertsX = gridX + 1, vertsZ = gridZ + 1;

    gldx::MeshData out;
    out.vertices.reserve(static_cast<std::size_t>(vertsX) * vertsZ);
    for (int j = 0; j < vertsZ; ++j) {
        const float fz = static_cast<float>(j) / gridZ;   // 0..1 across -hz..+hz
        for (int i = 0; i < vertsX; ++i) {
            const float fx = static_cast<float>(i) / gridX;
            gldx::Vertex v;
            v.position = {fx * 2.0f * hx - hx, 0.0f, fz * 2.0f * hz - hz};
            v.normal = {0.0f, 1.0f, 0.0f};   // the vertex shader overwrites this
            v.uv = {fx, fz};                 // global, so one map spans all tiles
            out.vertices.push_back(v);
        }
    }

    // Indices are emitted tile by tile, not row by row: Mesh::DrawRange can only
    // address a contiguous index span, so "one shared vertex grid, many index
    // spans" is what lets the sheet be sorted per tile without splitting the
    // surface at the seams. Both triangles of a cell are wound so their
    // right-hand normal is +y, matching the engine's GL_CCW / cull-BACK default.
    outTiles.clear();
    out.indices.reserve(static_cast<std::size_t>(gridX) * gridZ * 6);
    for (int tz = 0; tz < tilesZ; ++tz) {
        for (int tx = 0; tx < tilesX; ++tx) {
            const std::uint32_t start = static_cast<std::uint32_t>(out.indices.size());
            for (int j = 0; j < cellsPerTile; ++j) {
                const int jj = tz * cellsPerTile + j;
                for (int i = 0; i < cellsPerTile; ++i) {
                    const int ii = tx * cellsPerTile + i;
                    const std::uint32_t i0 = static_cast<std::uint32_t>(jj * vertsX + ii);
                    const std::uint32_t i1 = i0 + 1;
                    const std::uint32_t i2 = i0 + static_cast<std::uint32_t>(vertsX);
                    const std::uint32_t i3 = i2 + 1;
                    out.indices.insert(out.indices.end(), {i0, i1, i2, i2, i1, i3});
                }
            }
            Tile tile;
            tile.range.indexOffset = start;
            tile.range.indexCount = static_cast<std::uint32_t>(out.indices.size()) - start;
            tile.center = {(tx + 0.5f) / tilesX * 2.0f * hx - hx,
                           0.0f,
                           (tz + 0.5f) / tilesZ * 2.0f * hz - hz};
            outTiles.push_back(tile);
        }
    }

    out.ranges.clear();   // tiles are in outTiles, not in ranges: see below
    // Still publish them as ranges, because that is the only way the "sorting
    // off" comparison can draw the sheet in declaration order through one
    // Mesh::Draw() call and get exactly the same triangles in the wrong sequence.
    for (const Tile& t : outTiles) out.ranges.push_back(t.range);
    return out;
}

// ---- scene assembly ----------------------------------------------------------

Item& Scene::Add(Layer layer, int mesh, int tex, const glm::mat4& model,
                 const char* label) {
    // Every caller keeps the returned reference alive across later Add() calls
    // (`Item& basin = ...` and then twenty more items), which is only sound while
    // the table never reallocates. Build() reserves for the whole scene; if that
    // reserve is ever outgrown, say so here instead of handing back a reference
    // the next push_back silently invalidates.
    if (items_.size() == items_.capacity())
        std::fprintf(stderr, "glass_water: item table outgrew its reserve at '%s' - "
                     "references held from earlier Add() calls are now dangling\n", label);
    Item item;
    item.layer = layer;
    item.mesh = mesh;
    item.tex = tex;
    item.model = model;
    // Non-uniform scaling is the normal case here (a pane is a cube squeezed to
    // 2.6 x 3.2 x 0.06), and `mat3(model) * n` shears such a normal off the
    // surface it belongs to. The scene is static, so the inverse-transpose is
    // computed once, here, and never again.
    item.normalMatrix = glm::inverseTranspose(glm::mat3(model));
    item.center = glm::vec3(model[3]);
    item.label = label;
    items_.push_back(item);
    return items_.back();
}

void Scene::Build() {
    gldx::RenderContext::AssertRenderThread("Scene::Build");

    // ---- meshes (Stage A built here, Stage B uploaded immediately) ----------
    meshes_.clear();
    meshes_.reserve(kMeshCount);
    std::vector<gldx::MeshData> data(kMeshCount);
    data[kMeshDome] = gldx::GeometryFactory::Sphere(1.0f, 32, 16);
    data[kMeshFloor] = gldx::GeometryFactory::Plane(1.0f);
    data[kMeshCube] = gldx::GeometryFactory::Cube(1.0f);
    data[kMeshSphere] = gldx::GeometryFactory::Sphere(1.0f, 32, 16);
    waterTiles_.clear();
    data[kMeshWater] = MakeWaterGrid(18.0f, 12.0f, 6, 6, 8, waterTiles_);
    data[kMeshPanel] = gldx::GeometryFactory::Quad(3.4f, 2.6f);
    for (int i = 0; i < kMeshCount; ++i) {
        meshes_.emplace_back();
        meshes_.back().Upload(std::move(data[i]));
    }

    // ---- textures -----------------------------------------------------------
    textures_.clear();
    textures_.reserve(kTexCount);
    const auto addTex = [this](gldx::Texture2DDesc&& d) {
        textures_.emplace_back();
        textures_.back().Upload(d);
    };
    addTex(MakeWhiteDesc());
    addTex(MakeCheckerDesc());
    addTex(MakeCutoutDesc());
    addTex(MakeSkyDesc());

    // The world: a rectangular basin 36 x 24 units holding water at y = 0, its
    // floor 3.2 units below, and a promenade of opaque deck slabs whose top
    // surface is y = 0.15 - wide enough on the near side that the spawn point
    // stands *on* it rather than floating outside the pool, which is what keeps
    // the far side of a rim slab from filling the lower third of the frame as a
    // wall of checker. The glass / ice / bubble objects are then placed so that
    // something opaque is always visible *behind* every transparent one - the
    // only way the "depth test ate the pixels" failure is visible at all.
    items_.clear();
    // The pass hands out `const Item&`s and sorts a copy of the indices, so the
    // table itself must never move after Build() returns - reserve covers the
    // whole scene with room to spare.
    items_.reserve(48);

    // ---- sky ----------------------------------------------------------------
    {
        Item& it = Add(Layer::Sky, kMeshDome, kTexSky,
                       Box(glm::vec3(0.0f, -2.0f, -14.0f), glm::vec3(90.0f)),
                       "sky dome");
        it.surf.albedo = glm::vec3(1.0f);
        it.surf.lightMix = 0.0f;   // a backdrop, not a surface: the unlit branch
        it.surf.roughness = 1.0f;  // (albedo * map colour), so BRDF knobs are moot
    }

    // ---- opaque -------------------------------------------------------------
    Item& basin = Add(Layer::Opaque, kMeshFloor, kTexChecker,
                      Box(glm::vec3(0.0f, -3.2f, -14.0f), glm::vec3(24.0f, 1.0f, 20.0f)),
                      "basin floor");
    basin.surf.albedo = ToLinear({0.62f, 0.64f, 0.68f});
    basin.surf.roughness = 0.55f;   // sealed concrete: a broad, dim sheen
    basin.uvScale = {6.0f, 5.0f};

    const Placed kDeck[] = {
        // The near slab runs out past the camera's spawn at z = 15, so the first
        // thing in frame is the promenade surface, not the pool's outer wall.
        {{0.0f, -1.525f, 9.0f},     {44.0f, 3.35f, 17.0f}},   // near promenade
        {{0.0f, -1.525f, -28.0f},   {44.0f, 3.35f, 4.0f}},    // far rim
        {{-20.0f, -1.525f, -14.0f}, {4.0f, 3.35f, 24.0f}},    // west rim
        {{20.0f, -1.525f, -14.0f},  {4.0f, 3.35f, 24.0f}},    // east rim
    };
    for (const Placed& slab : kDeck) {
        Item& it = Add(Layer::Opaque, kMeshCube, kTexChecker, Box(slab.center, slab.size),
                       "deck slab");
        it.surf.albedo = ToLinear({0.78f, 0.76f, 0.72f});
        it.surf.roughness = 0.65f;  // weathered decking, no clearcoat to speak of
        it.uvScale = {slab.size.x * 0.25f, slab.size.z * 0.25f};
    }

    const Placed kPillars[] = {
        {{-9.0f, 0.4f, -8.0f},  {1.4f, 7.0f, 1.4f}},
        {{9.0f, 0.4f, -8.0f},   {1.4f, 7.0f, 1.4f}},
        {{-9.0f, 0.4f, -20.0f}, {1.4f, 7.0f, 1.4f}},
        {{9.0f, 0.4f, -20.0f},  {1.4f, 7.0f, 1.4f}},
    };
    for (const Placed& p : kPillars) {
        Item& it = Add(Layer::Opaque, kMeshCube, kTexChecker,
                       Box(p.center, p.size), "pillar");
        it.surf.albedo = ToLinear({0.70f, 0.68f, 0.64f});
        it.surf.roughness = 0.6f;
        it.uvScale = {0.5f, 2.0f};
    }

    // Three blocks that break the waterline at three different depths: one
    // barely wet, one knee-deep, one fully drowned. Looking straight down at the
    // last one through the sheet is the clearest read on the water's baseAlpha.
    const Placed kPiers[] = {
        {{0.0f, -0.2f, -6.0f},    {1.4f, 1.4f, 1.4f}},
        {{-13.0f, 0.1f, -16.0f},  {1.4f, 1.4f, 1.4f}},
        {{13.0f, -1.4f, -22.0f},  {1.4f, 1.4f, 1.4f}},
    };
    for (const Placed& p : kPiers) {
        Item& it = Add(Layer::Opaque, kMeshCube, kTexChecker,
                       Box(p.center, p.size), "pier block");
        it.surf.albedo = ToLinear({0.55f, 0.57f, 0.60f});
        it.surf.roughness = 0.5f;   // the drowned one has a wet sheen on it
        it.uvScale = {0.5f, 0.5f};
    }

    const Placed kBoulders[] = {
        {{-6.0f, -1.0f, -18.0f}, {2.2f, 2.2f, 2.2f}},
        {{7.0f, -2.6f, -24.0f},  {1.2f, 1.2f, 1.2f}},
    };
    for (const Placed& b : kBoulders) {
        Item& it = Add(Layer::Opaque, kMeshSphere, kTexChecker,
                       Box(b.center, b.size), "boulder");
        it.surf.albedo = ToLinear({0.44f, 0.46f, 0.48f});
        it.surf.roughness = 0.45f;   // tumbling-polished stone, still not a mirror
        it.uvScale = {1.0f, 1.0f};
    }

    // ---- cutout (alpha-tested, and therefore *not* part of the blend layer) --
    // The rail along the far rim and the two screens standing in the pool all
    // discard their holes instead of blending them, so they keep depth writes
    // and no sorting: what shows through a hole is a real depth-buffer gap.
    for (int i = -3; i <= 3; ++i) {
        Item& it = Add(Layer::Cutout, kMeshPanel, kTexCutout,
                       Box({static_cast<float>(i) * 3.4f, 1.45f, -28.0f}, {1.0f, 1.0f, 1.0f}),
                       "fence rail");
        it.surf.albedo = ToLinear({0.66f, 0.55f, 0.40f});
        it.surf.roughness = 0.6f;    // painted timber
        it.surf.alphaCutoff = 0.5f;
        it.doubleSided = true;   // a quad has one face; the rail is seen from both
    }
    for (float x : {-3.4f, 0.4f}) {
        Item& it = Add(Layer::Cutout, kMeshPanel, kTexCutout,
                       Box({x, 1.35f, -20.0f}, {1.0f, 1.0f, 1.0f}), "lattice screen");
        it.surf.albedo = ToLinear({0.72f, 0.74f, 0.76f});
        it.surf.roughness = 0.35f;   // galvanised steel: a tight, smooth sheen, and
                                     // still no depth writes to lose - it is a cutout
        it.surf.alphaCutoff = 0.5f;
        it.doubleSided = true;
    }

    // ---- blend: the water ---------------------------------------------------
    {
        Item& it = Add(Layer::Blend, kMeshWater, kTexWhite,
                       Box(glm::vec3(0.0f, 0.0f, -14.0f), glm::vec3(1.0f)), "water sheet");
        it.surf.albedo = ToLinear({0.20f, 0.46f, 0.54f});
        // Everything below is why this is a PBR sheet and not a blue quad with an
        // alpha. IOR 1.333 derives F0 = 2.0 %: water simply cannot mirror much
        // looking straight down, and any "see-through blue" that does is lying.
        it.surf.ior = 1.333f;
        it.surf.roughness = 0.06f;    // a wind-roughed but still mirror-smooth surface
        // The one place in the scene with a volume. Red dies first, blue last, in
        // the ratio real water has; and the path the shader eats them along is
        // pathLen / NoV, so the same 3.2 units of basin go from "you can read the
        // checker tiles" overhead to "a wall of teal" at the far edge. The scale is
        // a third of clear-seawater extinction - a pool, not an ocean - but it is
        // still the ratio (roughly 4:1:1.4 red:green:blue) that lets the shader say
        // "this is water" without a single texture lookup.
        it.surf.absorption = {0.34f, 0.085f, 0.115f};
        it.surf.pathLen = 3.2f;       // surface y=0 down to the floor at y=-3.2
        // Not an opacity any more - a floor for the two things that are physically
        // derived (reflection and absorption): suspended silt, essentially.
        it.surf.baseAlpha = 0.05f;
        it.surf.fresnelToAlpha = 1.0f;  // strict conservation: mirror == hide
        it.surf.waveAmp = 1.0f;          // a flag: the pass scales it by the live sea state
        it.doubleSided = true;           // swum under, the surface has to read from below
        it.tiles = &waterTiles_;
        // Baked into world space: the sheet's own model matrix is a pure
        // translation, so the local tile centres Build() got back just need it.
        for (Tile& t : waterTiles_) t.center += glm::vec3(0.0f, 0.0f, -14.0f);
    }

    // ---- blend: the transparency ladder -------------------------------------
    // Five identical panes whose only difference is baseAlpha, spaced across the
    // near edge of the pool so the basin, the pillars and the water behind them
    // are all in the frame at once. This is the row the whole demo is named for:
    // the same object, five opacities, and the pixels behind it must survive all
    // five.
    const float kLadder[] = {0.10f, 0.20f, 0.30f, 0.50f, 0.80f};
    // String literals, not formatted ones: `Item::label` is a borrowed pointer
    // that the HUD dereferences many frames later, so it has to outlive Build().
    static const char* const kPaneLabels[] = {
        "pane a=0.10", "pane a=0.20", "pane a=0.30", "pane a=0.50", "pane a=0.80"};
    for (int i = 0; i < 5; ++i) {
        Item& it = Add(Layer::Blend, kMeshCube, kTexWhite,
                       Box({-8.0f + i * 4.0f, 1.75f, 4.0f}, {2.6f, 3.2f, 0.06f}),
                       kPaneLabels[i]);
        // A pane is not a white card: what makes glass visible is the small
        // fraction of light it removes, so the body tint stays dark and the
        // ladder shows up as "how much of the test card behind me survives".
        // A near-white albedo here turns every step of the ladder into the same
        // opaque rectangle and the demo stops being able to say anything.
        it.surf.albedo = ToLinear({0.44f, 0.62f, 0.66f});
        it.surf.ior = 1.52f;        // soda-lime glass: F0 = 4.0 %, twice the water
        it.surf.roughness = 0.05f;  // polished, so the sun glint stays a glint
        it.surf.baseAlpha = kLadder[i];
        it.surf.fresnelToAlpha = 0.95f;
        it.shellTwoPass = true;   // a closed slab: far face first, then near face
    }

    // ---- blend: tinted, curved and frosted ----------------------------------
    {
        // Parked to the left of the pane row, in open water, on purpose. The
        // chest is the blend layer's designated victim for the depth-write
        // switch: it sits just under the surface, so the water tile covering it
        // is a sibling primitive that can be submitted before it. The moment the
        // blend layer writes depth again, that tile's depth rejects the chest and
        // a whole object disappears - the failure this demo is named for. Behind
        // the panes it would have been attenuated into invisibility in the
        // reference frame, and an artefact nobody can see proves nothing. The
        // opaque pier block a few metres further out is the control: it is drawn
        // before any blending happens, so no transparency mistake can eat it.
        Item& it = Add(Layer::Blend, kMeshCube, kTexWhite,
                       Box({-14.0f, -0.9f, -10.0f}, {1.4f, 1.4f, 1.4f}), "stained chest");
        it.surf.albedo = ToLinear({0.24f, 0.86f, 0.46f});
        it.surf.ior = 1.5f;
        it.surf.roughness = 0.3f;     // resin over old paint: a soft, wide highlight
        it.surf.baseAlpha = 0.62f;
        it.surf.fresnelToAlpha = 0.4f;  // it under-reports its own reflectance
        it.shellTwoPass = true;
    }
    {
        Item& it = Add(Layer::Blend, kMeshSphere, kTexWhite,
                       Ball({-5.0f, 1.2f, -10.0f}, 1.3f), "soap bubble");
        it.surf.albedo = ToLinear({0.72f, 0.80f, 0.88f});
        it.surf.ior = 1.4f;           // a soap film is barely there: F0 = 2.9 %
        it.surf.roughness = 0.03f;    // so clean the glint is a needle
        it.surf.baseAlpha = 0.04f;
        it.surf.fresnelToAlpha = 1.0f;   // the film hides exactly what it mirrors
        it.surf.iridescence = 0.9f;
        it.shellTwoPass = true;
    }
    {
        Item& it = Add(Layer::Blend, kMeshCube, kTexWhite,
                       Box({0.0f, 1.6f, -24.0f}, {2.4f, 3.0f, 0.35f}), "frosted slab");
        it.surf.albedo = ToLinear({0.86f, 0.90f, 0.94f});
        it.surf.ior = 1.45f;
        // The lesson this one object exists to teach: roughness is not opacity.
        // A diffusing surface still conserves energy and still has a Fresnel, but
        // its lobe is so wide that the environment smear becomes a milky glow
        // rather than a mirror - which no amount of baseAlpha can fake.
        it.surf.roughness = 0.62f;
        it.surf.baseAlpha = 0.72f;      // almost opaque, but it must not write depth
        it.surf.fresnelToAlpha = 0.12f;
        it.shellTwoPass = true;
    }

    // ---- glow (additive, drawn last) ----------------------------------------
    // Each lamp is the nearest transparent thing in its part of the frame, which
    // is what makes "additive last, unsorted" legitimate here rather than a
    // second bug stacked on the first: a glow that ought to sit *behind* a
    // blended surface would have to join the Blend layer instead, because
    // Glow-last means nothing can be composited over it.
    const std::pair<glm::vec3, float> kLamps[] = {
        {{0.0f, 2.6f, -22.0f}, 0.5f},    // in front of the frosted slab
        {{-12.0f, 0.9f, 2.0f}, 0.35f},   // over the near deck
        {{11.0f, 5.0f, -14.0f}, 0.4f},   // high above the pool
    };
    for (const auto& lamp : kLamps) {
        Item& it = Add(Layer::Glow, kMeshSphere, kTexWhite, Ball(lamp.first, lamp.second),
                       "glow lamp");
        it.surf.albedo = ToLinear({1.0f, 0.72f, 0.40f});
        it.surf.baseAlpha = 0.0f;
        it.surf.coreToAlpha = 1.6f;    // face-on opaque, feathered to nothing at the rim
        it.surf.emissive = 1.8f;       // >1 is what the bloom threshold keys on
        it.surf.lightMix = 0.0f;       // a light source is not lit by the sun
    }
}

} // namespace glass_water
