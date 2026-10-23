#ifndef GLASS_WATER_SCENE_H
#define GLASS_WATER_SCENE_H

/**
 * @file Scene.h
 * @brief The glass_water scene: a flat item table plus the CPU-side geometry and
 *        textures it draws with.
 *
 * Everything here is Stage A data plus one render-thread `Build()` that ships it
 * to the GPU. There is no scene graph on purpose: this demo is about *when* a
 * fragment is allowed to touch the framebuffer, so the item list is kept as an
 * array the pass can sort, and each item carries the handful of numbers that
 * make it glass, water, ice or a lamp (see `Surface`). The behavioural half -
 * blend function, depth writes, cull mode, draw order - is not here at all; it
 * lives in BlendPass.cpp, which is the point of splitting it this way.
 *
 * The one piece of geometry that is not just "another copy of the unit cube" is
 * the water sheet, and it is the reason `Tile` exists. A back-to-front sort can
 * only be as correct as its granularity: the sheet is 36 x 24 world units of
 * translucent surface with a single centre, so sorting it as one object means
 * one half of it is always mis-ordered against anything else it overlaps.
 * MakeWaterGrid therefore cuts the sheet into independently indexable tiles -
 * contiguous GeometryRange spans over one shared vertex grid, so the waves stay
 * welded across tile seams while each tile can still be drawn on its own.
 *
 * NOTE: names gldx engine types, so include AFTER `import gldx;`.
 */

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace glass_water {

// Which of the five pipeline states an item is drawn with. The order below IS
// the draw order of the layers (Sky, then the depth-writing opaque pair, then
// the blended pair) - see BlendPass.cpp.
enum class Layer : std::uint8_t {
    Sky,      // no depth test, no depth write, drawn first and unlit
    Opaque,   // depth write on, blending off, back-face culled
    Cutout,   // alpha-tested: `discard` leaves real holes, so it may keep depth writes
    Blend,    // the transparent layer: depth writes off by default, sorted far -> near
    Glow,     // additive: order-independent against itself, so it comes last
};

// The three ways of combining source and destination this demo switches between.
// The first two differ only in whether the fragment stage pre-multiplies its
// rgb; the third is a different equation altogether.
enum class BlendMode : std::uint8_t {
    Straight,       // (SRC_ALPHA, ONE_MINUS_SRC_ALPHA)
    Premultiplied,  // (ONE, ONE_MINUS_SRC_ALPHA) + rgb *= a in the shader
    Additive,       // (SRC_ALPHA, ONE): light adds, nothing is ever subtracted
};

// Per-item shading parameters. Every field maps 1:1 onto a uniform in
// Shaders.h; there is no second shader variant for "transparent things".
//
// The material half is a dielectric Cook-Torrance model, so the two knobs that
// actually define a non-metal are the index of refraction and the roughness:
// F0 comes out of the IOR rather than being painted in, and the highlight's
// size comes out of the roughness rather than being a second, disagreeing
// exponent. That is why the old `specular` / `shininess` / `reflect` /
// `fresnelPower` set is gone - under a Fresnel that is physically derived, all
// four were different hand-tuned escapes from the same two numbers, and a
// transparency demo cannot afford escapes that let a surface reflect more than it
// conserves.
struct Surface {
    glm::vec3 albedo{1.0f};
    // ---- what the material is made of ---------------------------------------
    float ior       = 1.5f;      // dielectric F0 = ((ior-1)/(ior+1))^2, so water
                                 // 1.333 mirrors far less than glass at 1.52
    float roughness = 0.5f;      // GGX alpha: 0 = mirror, 1 = pure diffuser
    // Beer-Lambert extinction coefficient per world unit of travelled light
    // path. Zero everywhere but the water: what makes looking *down* through a
    // thick slice of it go teal while a grazing path through the same sheet
    // stays clear.
    glm::vec3 absorption{0.0f};
    // How thick the body the light crosses is, in world units, for the absorption
    // above to eat through. For the water sheet that is the depth of the basin;
    // the shader divides it by NoV to get the slant path, which is the whole
    // near-versus-far effect. 0 means "no volume, a surface film" - the panes.
    float pathLen = 0.0f;
    // ---- how a see-through surface spends its alpha -------------------------
    // baseAlpha is the floor (for the water, the tint left after absorption);
    // fresnelToAlpha is the coupling from reflectance to opacity. 1.0 means
    // strict energy conservation - however much the surface mirrors at this
    // angle is exactly how much of what is behind it gets hidden. Anything
    // below 1 is a surface that lies about being a mirror.
    float baseAlpha      = 1.0f;
    float fresnelToAlpha = 0.0f;   // grazing angle -> opaque (glass, water)
    float coreToAlpha    = 0.0f;   // facing angle -> opaque (additive glow ball)
    float alphaCutoff    = 0.0f;   // >0 only for the cutout lattice
    float iridescence    = 0.0f;   // thin-film hue shift, keyed on the Fresnel
    float emissive       = 0.0f;   // HDR multiplier; >1 is what bloom keys on
    float lightMix       = 1.0f;   // 0 for the sky backdrop and the lamps
    float waveAmp        = 0.0f;   // >0 only for the water sheet
};

// One sortable piece of the water sheet: an index range plus the world-space
// centre it is sorted against. Baked at build time because the scene never moves.
struct Tile {
    gldx::GeometryRange range;
    glm::vec3 center{0.0f};
};

// One drawable thing. `mesh` and `tex` are indices into Scene::meshes() /
// textures() rather than pointers, so items stay a plain copyable POD table that
// the pass is free to sort and re-order every frame.
struct Item {
    Layer layer = Layer::Opaque;
    int mesh = 0;
    int tex = 0;
    glm::mat4 model{1.0f};
    glm::mat3 normalMatrix{1.0f};   // inverse-transpose of model, baked once
    glm::vec3 center{0.0f};         // world centre, the key of the far->near sort
    glm::vec2 uvScale{1.0f};
    bool shellTwoPass = false;      // closed transparent skin: draw far side, then near
    bool doubleSided = false;       // sheets that must read correctly from behind
    const std::vector<Tile>* tiles = nullptr;   // water sheet only
    Surface surf;
    const char* label = "";
};

// Mesh slots, filled by Scene::Build() in this order.
enum MeshSlot : int {
    kMeshDome = 0,   // sky sphere, wound so its inside is the front face
    kMeshFloor,      // Plane(1)
    kMeshCube,       // Cube(1)
    kMeshSphere,     // Sphere(1)
    kMeshWater,      // MakeWaterGrid(), tiled
    kMeshPanel,      // Quad(3.4, 2.6), the cutout lattice sheet
    kMeshCount
};

// Texture slots, same order.
enum TexSlot : int {
    kTexWhite = 0,   // 1x1 opaque, a flat fill for everything with no map
    kTexChecker,     // 3-channel basin / deck pattern
    kTexCutout,      // 4-channel lattice: the holes are alpha 0
    kTexSky,         // vertical gradient, sampled by the dome's uv
    kTexCount
};

// CPU-side texture generators (Stage A, no GL).
gldx::Texture2DDesc MakeWhiteDesc();
gldx::Texture2DDesc MakeCheckerDesc(int size = 512, int cells = 4);
gldx::Texture2DDesc MakeCutoutDesc(int size = 256, int bars = 8);
gldx::Texture2DDesc MakeSkyDesc(int height = 256);

// The sky's three bands, in linear radiance. Defined once in Scene.cpp and used
// twice: to paint the dome's gradient map, and handed to the shader as the
// environment every PBR surface reflects. They must not drift apart, or the
// water mirrors a sky the dome is not showing - the single most common way a
// fake-environment reflection reads as a tint rather than as a reflection.
extern const glm::vec3 kSkyZenith;
extern const glm::vec3 kSkyHorizon;
extern const glm::vec3 kSkyNadir;

// The sun as an HDR radiance rather than a white multiplier, because under a
// linear pipeline that is the only way it can both light a basin and key bloom.
// Anything near 1.0 and below and the scene reads as a flat matte painting; the
// ACES stage needs real headroom to have a highlight to compress.
extern const glm::vec3 kSunColor;

// The tiled water sheet. `hx` / `hz` are the half extents in X/Z, the sheet is
// centred on the local origin, and it is cut into tilesX x tilesZ blocks of
// `cellsPerTile` quads each. One vertex grid is shared across the whole sheet
// (so a wave never splits at a tile seam) while each tile's indices are written
// contiguously, which is exactly what Mesh::DrawRange needs. Tile centres are
// returned in LOCAL space; Build() offsets them into world space.
gldx::MeshData MakeWaterGrid(float hx, float hz, int tilesX, int tilesZ,
                             int cellsPerTile, std::vector<Tile>& outTiles);

class Scene {
public:
    // Uploads the meshes + textures and fills the item table. Render-thread only.
    void Build();

    [[nodiscard]] const std::vector<Item>& items() const noexcept { return items_; }
    [[nodiscard]] const std::vector<gldx::Mesh>& meshes() const noexcept { return meshes_; }
    [[nodiscard]] const std::vector<gldx::Texture2D>& textures() const noexcept { return textures_; }

    // Filled by Build(): the direction the single sun points *toward*. The HUD and
    // the pass both read it. The sky the surfaces reflect is not a colour here but
    // the kSkyZenith/kSkyHorizon/kSkyNadir band triple, shared with the dome map.
    glm::vec3 sunToward{0.42f, 0.78f, 0.46f};

private:
    // Appends one item. Every mesh in this demo is centred on its own local
    // origin, so the translation column of `model` is its world centre and the
    // sort key - nothing has to be stated twice.
    Item& Add(Layer layer, int mesh, int tex, const glm::mat4& model,
              const char* label);

    std::vector<gldx::Mesh> meshes_;
    std::vector<gldx::Texture2D> textures_;
    std::vector<Tile> waterTiles_;
    std::vector<Item> items_;
};

} // namespace glass_water

#endif // GLASS_WATER_SCENE_H
