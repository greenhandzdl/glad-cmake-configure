#ifndef NIGHT_FOREST_SCENE_H
#define NIGHT_FOREST_SCENE_H

/**
 * @file Scene.h
 * @brief The night_forest world: everything that is drawn, as instances.
 *
 * The demo's claim is that a forest is cheap, and the claim lives here. The whole
 * world is five GPU objects and they are all of them `InstancedMesh`: one ground
 * plane (one instance), two tree species (~700 instances each), one crate (one
 * instance, re-uploaded every frame) and one lantern (four instances). Fourteen
 * hundred trees are two draw calls because a tree is *one* mesh: trunk and three
 * canopy tiers are merged into a single vertex buffer on the CPU, and the two flat
 * tones come from the vertex colour the engine's InstancedMesh now forwards to
 * location 9. Had the tones come from the instance instead, bark and leaves
 * would have needed separate instance arrays and the field would have cost four
 * draws rather than two - which is the mistake this file exists to avoid making.
 *
 * Per-instance data is only ever a rigid transform plus a tint: translation, a
 * rotation about Y, and ONE uniform scale. That is not a stylistic limit, it is
 * what the vertex stage can do without an inverse (see kWorldVertex - it uses
 * mat3(aInstance) and normalise() as the normal matrix, which is only correct
 * while the scale is isotropic). Any squash wanted in the shape itself, like a
 * broadleaf canopy flattened into a loaf, is baked into the mesh here on the CPU
 * where affineInverse() is available.
 *
 * The vertex colour's alpha is not opacity. It is a per-vertex material mask -
 * emissive strength and, through it, how much of a surface may be spent on the
 * environment cube - because a per-draw uniform cannot say "this half of the
 * mesh is a flame and this half is the iron around it" and the lantern needs
 * exactly that. Shaders.h documents the other side of the arrangement.
 *
 * The crate's wander is a table, not an integrator. `MakeRoamTable` advances a
 * fixed step from a seeded RNG once at startup and stores every pose; sampling
 * it is a lookup and a lerp. That is what makes a randomly-moving object
 * screenshot-reproducible: `--freeze-at` picks a table index rather than hoping
 * the frame rate was the same twice, and the same seed at the same second is the
 * same pose on any machine. The walking camera uses the same mechanism, which is
 * the only reason "climbing over ridges and through gullies" can appear in a
 * documentation screenshot at all - a
 * keyboard-driven camera moves by `dt`, so its pose belongs to the frame rate.
 *
 * The ground is the other half of the experiment: either the engine's single
 * textured quad (the default, and the sheet every existing screenshot of this demo
 * was taken on) or a height field built by `Terrain`, in which case the trees, the
 * lanterns, the crate and the camera are all placed on its surface rather than on
 * y = 0. Scene.h's `heightAt` is the single question everything asks.
 *
 * NOTE: names gldx engine types, so include AFTER `import gldx;`.
 */

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "Terrain.h"

namespace night_forest {

// ---- the crate's scripted wander ------------------------------------------
struct Pose {
    glm::vec3 pos{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;       // radians about +Y; the crate's face leads
};

struct Wander {
    double startAt     = 3.0;    // "a certain moment": still before this, moving after
    double stepSeconds = 0.1;    // fixed, so a pose is a table index rather than a dt sum
    double horizon     = 600.0;  // seconds of path precomputed at startup
    float  speed       = 1.15f;  // metres per second
    float  radius      = 16.0f;  // roam area; the path steers back at the edge
    float  turnChance  = 0.05f;  // per-step probability of a heading nudge
    float  turnMax     = 0.85f;  // radians
    float  size        = 1.25f;  // cube edge in metres
};

// The full path, from the seed alone. Deterministic by construction: one
// mt19937, one fixed step, no dependence on how many frames were rendered.
std::vector<Pose> MakeRoamTable(const Wander& w, std::uint32_t seed);

// The pose at wall-clock `seconds`. Before `w.startAt` this is the parked pose,
// and past the table's end it is the last one, so a long-running session cannot
// walk off the array.
Pose SampleRoam(const std::vector<Pose>& table, const Wander& w, double seconds);

// Instance matrix for a crate pose. Translation + Y rotation + one uniform
// scale, which is the contract kWorldVertex's normal handling depends on.
glm::mat4 CrateTransform(const Pose& p, float size);

// ---- scene configuration (Stage A, read once by Build) --------------------

// The walking camera's own defaults, so that it does not inherit the crate's:
// a person's pace rather than a sliding box's, a path that leaves the clearing and
// crosses the tree band (that is where the hills are), and no cube edge - the
// wander table does not know what it is carrying, and `size` is the crate's
// business. Turn nudges are gentler too, because an eye that swings 0.85 rad in one
// 0.1 s step is a camera the viewer cannot follow.
inline Wander MakeWalkDefaults() {
    Wander w;
    w.speed      = 1.4f;
    w.radius     = 55.0f;
    w.size       = 0.0f;
    w.turnChance = 0.04f;
    w.turnMax    = 0.55f;
    w.startAt    = 0.0;
    return w;
}

struct Config {
    int    treeCount = 1400;
    std::uint32_t seed = 20261104u;
    float  ringMin   = 9.0f;     // inner edge of the tree band (metres)
    float  ringMax   = 95.0f;    // outer edge; fog swallows anything past ~160
    float  scaleMin  = 0.65f;
    float  scaleMax  = 2.30f;
    int    firShare  = 58;       // percent of the field that is conifer
    float  clearing  = 6.5f;     // kept treeless, so the crate has room to be seen
    float  minSpacing = 3.1f;    // rejection radius, keeps canopies from merging
    float  groundHalf = 200.0f;  // the plane's instance scale
    int    grassTile  = 48;      // uv repeats across the whole plane
    Wander wander;

    // ---- the height field (Terrain.h): off by default ----------------------
    // `flat` keeps the single quad above, so every frame this demo has ever been
    // photographed with is still the frame it is now. The manual's terrain modes
    // are one switch away, and the two are mutually exclusive by construction:
    // whichever ground is drawn, `heightAt` is the authority on where it is.
    TerrainDesc terrain;
    // Metres of ground map per tile on the height field. 0 derives it from
    // `grassTile`, so switching to a terrain changes the relief, not the density
    // of the grass - the two comparisons the documentation then makes stay clean.
    float  terrainTile = 0.0f;
    Wander walk = MakeWalkDefaults();   // the walking camera's path
    float  walkEye = 1.7f;       // metres above the surface
};

// ---- CPU-side geometry (Stage A) ------------------------------------------
// A conifer: a tapered trunk and three faceted cones, each with its own vertex
// colour. Faceted rather than smooth on purpose - under a Gooch band a flat
// shade IS the brush stroke, and a smooth canopy turns the three cone tiers into
// one green bulb.
gldx::MeshData MakeFirMesh();
// A broadleaf: the same trunk, one sphere canopy flattened into a loaf. The
// squash is baked here (non-uniform scale), not handed to the instance.
gldx::MeshData MakeBroadleafMesh();
// The crate: a cube whose faces each carry the full 0..1 uv, so the plank map
// lands one panel per face, and whose alpha channel is 1 - the per-vertex
// emissive mask (Shaders.h) says "this surface may spend some of its pixels on
// the environment cube".
gldx::MeshData MakeCrateMesh();
// A lantern: an iron frame (mask 0) around a glowing core (mask 1). Four of
// these are also four point lights, so the geometry and the light setup in
// main.cpp must come from the same positions - Scene keeps that list.
gldx::MeshData MakeLanternMesh();
// Ground: the engine's Plane, which is 4 vertices in the XZ plane with uv 0..1
// across the whole sheet. Size arrives as an instance scale, so the tiling has
// to come from the uv scale uniform rather than from the vertex data.
gldx::MeshData MakeGroundMesh();

// ---- CPU-side textures (Stage A) -----------------------------------------
gldx::Texture2DDesc MakeWhiteDesc();
gldx::Texture2DDesc MakeGrassDesc(int size = 512, std::uint32_t seed = 0x9e37u);
gldx::Texture2DDesc MakeWoodDesc(int size = 512);

// The world. Owns the GPU resources; Build() must run on the render thread and
// the object must die before the context does (main.cpp keeps it inside the
// lambda that owns the window's scope).
class Scene {
public:
    Config config;

    // Uploads geometry + textures and fills every instance array. The tree
    // scatter runs here, so `config.seed` alone decides the forest - and two runs
    // of one seed draw the same triangles in the same order.
    void Build();

    // Repositions the crate. Render-thread only: it re-uploads ONE instance, so
    // the moving object costs a 32-byte buffer write while the forest behind it
    // is never touched again.
    void UpdateCrate(double seconds, bool roaming);

    // ---- the ground --------------------------------------------------------
    // The surface height in metres at world (x, z): `Terrain::GetAveHeight` when a
    // height field is built, and an exact 0 when the flat sheet is, which is what
    // lets the scatter, the lanterns, the crate and the walking camera ask one
    // question without a mode branch each - and what keeps the default frame's
    // bits unchanged by the whole feature.
    [[nodiscard]] float heightAt(float x, float z) const noexcept;

    // The walking camera's pose at wall-clock `seconds`: the same fixed-step table
    // as the crate's, with the ground's height substituted for a `dt` integral, so
    // Crossing the hills is reproducible rather than frame-rate-shaped. `pos` already
    // carries the eye height; `yaw` is the heading, and the camera's pitch stays
    // whoever owns it (the path does not look up and down for you).
    [[nodiscard]] Pose SampleWalk(double seconds) const;

    [[nodiscard]] const Terrain& terrain() const noexcept { return terrain_; }
    // uv repeats per unit of vertex uv. Flat ground has uv 0..1 across the sheet
    // and wants `grassTile` of them; a height field carries the manual's integer
    // lattice as uv, so its scale is tiles-per-cell instead.
    [[nodiscard]] float groundUvScale() const noexcept;

    // The four lantern positions, which main.cpp turns into point lights. Shared
    // by construction with the geometry, so a glow cannot sit where no lamp is.
    [[nodiscard]] const std::vector<glm::vec3>& lanterns() const noexcept { return lanternPos_; }

    [[nodiscard]] const gldx::InstancedMesh& ground()    const noexcept { return ground_; }
    [[nodiscard]] const gldx::InstancedMesh& fir()       const noexcept { return fir_; }
    [[nodiscard]] const gldx::InstancedMesh& broadleaf() const noexcept { return broadleaf_; }
    [[nodiscard]] const gldx::InstancedMesh& crate()     const noexcept { return crate_; }
    [[nodiscard]] const gldx::InstancedMesh& lantern()   const noexcept { return lantern_; }

    [[nodiscard]] const gldx::Texture2D& grass()  const noexcept { return grass_; }
    [[nodiscard]] const gldx::Texture2D& wood()   const noexcept { return wood_; }
    [[nodiscard]] const gldx::Texture2D& white()  const noexcept { return white_; }

    [[nodiscard]] int grassTile() const noexcept { return config.grassTile; }
    [[nodiscard]] Pose cratePose() const noexcept { return cratePose_; }
    [[nodiscard]] const std::vector<Pose>& roamTable() const noexcept { return roam_; }

    // Evidence for the HUD: what the field actually ended up holding. The
    // scatter rejects candidates that are too close or outside the band, so the
    // requested count and the planted count differ, and a panel that printed the
    // request would be printing a wish.
    [[nodiscard]] std::uint32_t plantedFir()  const noexcept { return fir_.count(); }
    [[nodiscard]] std::uint32_t plantedBroad() const noexcept { return broadleaf_.count(); }
    [[nodiscard]] std::uint32_t rejected()   const noexcept { return rejected_; }
    [[nodiscard]] float groundHalf() const noexcept { return config.groundHalf; }
    [[nodiscard]] float crateSize() const noexcept { return config.wander.size; }

private:
    // The crate's table pose, put on the surface (see Scene.cpp).
    [[nodiscard]] Pose GroundCratePose(const Pose& p) const;

    gldx::InstancedMesh ground_, fir_, broadleaf_, crate_, lantern_;
    gldx::Texture2D grass_, wood_, white_;
    Terrain terrain_;
    std::vector<glm::vec3> lanternPos_;
    std::vector<Pose> roam_;
    std::vector<Pose> walk_;      // the camera's path, same shape as roam_'s
    Pose cratePose_;
    std::uint32_t rejected_ = 0;
};

} // namespace night_forest

#endif // NIGHT_FOREST_SCENE_H
