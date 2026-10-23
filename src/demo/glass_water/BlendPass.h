#ifndef GLASS_WATER_BLENDPASS_H
#define GLASS_WATER_BLENDPASS_H

/**
 * @file BlendPass.h
 * @brief The whole glass_water frame: five layers, one program, and the pipeline
 *        state that decides whether a transparent fragment may spend the pixels
 *        behind it.
 *
 * This pass is the demo. Everything in Scene.h is data and everything in
 * Shaders.h is "how much light does this pixel mirror, absorb, and let through";
 * *whether* that pixel is allowed to overwrite what is already in the colour and
 * depth buffers is decided here, by four choices that are each independently
 * switchable:
 *
 *   1. depth writes   - the blend layer must not fill the depth buffer, or the
 *                       opaque geometry behind it fails GL_LESS and simply never
 *                       gets drawn: the "the object behind disappeared" bug.
 *   2. ordering       - with depth writes off there is no longer any inter-layer
 *                       occlusion, so the only thing left holding the composite
 *                       together is drawing far things before near things. And
 *                       that is only true down to the granularity you sort at,
 *                       which is why the water sheet enters as 36 tiles.
 *   3. the shell pass - a closed transparent body (a pane, a bubble, a chest) has
 *                       two surfaces along every view ray and no sorting can put
 *                       them in an item's order. Drawing the far skin first and
 *                       the near skin second is the only way it composites right.
 *   4. the equation   - straight, premultiplied and additive alpha are three
 *                       different answers to "what does a=0.5 mean", and each one
 *                       obliges the fragment stage to hand over its rgb
 *                       differently. The coverage now comes out of conservation,
 *                       so the pair (shader rgb weighting, blend factors) is a
 *                       contract rather than a house style: exactly one side may
 *                       multiply by alpha. Get it wrong in one direction and
 *                       layers add instead of attenuating; get it wrong in the
 *                       other and they are weighted twice.
 *
 * Everything the pass decides is also read back out of GL and published for the
 * HUD (`appliedState()`), so the overlay reports what the driver was actually
 * told rather than what this file meant to tell it.
 *
 * NOTE: names gldx engine types, so include AFTER `import gldx;`.
 */

#include <vector>

#include <glm/glm.hpp>

#include "Scene.h"

namespace glass_water {

class BlendPass final : public gldx::RenderPass {
public:
    BlendPass() : gldx::RenderPass("Blend") {}

    // Compiles the single program the entire scene is drawn with. Render-thread
    // only; false means the caller should not run the frame loop at all.
    bool Init();

    struct Settings {
        const Scene* scene = nullptr;
        // The four correctness switches, straight from Input.
        bool sortBackToFront = true;
        bool blendWritesDepth = false;
        bool shellTwoPass = true;
        BlendMode blend = BlendMode::Premultiplied;
        // Diagnostics.
        bool cull = true;
        bool wireframe = false;
        bool glow = true;
        float wave = 0.22f;       // sea state; 0 flattens the sheet
        float time = 0.0f;        // seconds, drives the vertex-shader wave field
        glm::vec3 sunToward{0.42f, 0.78f, 0.46f};
        // Which projection uViewProj carries. The geometry does not care; the
        // viewing direction in the BRDF very much does, because an orthographic
        // frustum has no eye position to subtract from (see Shaders.h).
        bool orthographic = false;
        glm::vec3 camForward{0.0f, 0.0f, -1.0f};
    };
    Settings settings;

    void Execute(gldx::RenderFrame& f) override;

    // ---- evidence the HUD prints ---------------------------------------------
    // One primitive as the blend layer actually submitted it.
    struct Entry {
        const char* label = "";
        float distance = 0.0f;   // centre-to-eye, the sort key
        int tile = -1;           // >=0 when this is one tile of the water sheet
    };
    // The order the last Execute() drew the blended primitives in, far first.
    // Empty when sorting is off, because then there is no order to report - the
    // sheet goes out as one submission in declaration order instead.
    const std::vector<Entry>& order() const noexcept { return order_; }
    [[nodiscard]] int blendDraws() const noexcept { return blendDraws_; }
    [[nodiscard]] int totalDraws() const noexcept { return totalDraws_; }

    // The pipeline state read back out of GL at the end of the blended layer -
    // not a copy of what the code asked for.
    struct GLState {
        bool depthTest = false;
        bool depthWrite = false;
        bool blend = false;
        bool cull = false;
        int srcRGB = 0;
        int dstRGB = 0;
    };
    [[nodiscard]] GLState appliedState() const noexcept { return glState_; }

private:
    // One sortable primitive: an item, or one tile of an item that is split.
    struct Sortable {
        int item = 0;
        int tile = -1;
        glm::vec3 center{0.0f};
    };

    void DrawItem(const gldx::ShaderProgram& p, const Item& item, const Sortable* prim);
    void ApplyBlendState(BlendMode mode, bool writeDepth, bool cull);

    gldx::ShaderProgram prog_;
    // Set alongside ApplyBlendState so the fragment stage's rgb weighting always
    // matches the equation that is live; see Execute().
    bool premul_ = false;
    std::vector<Sortable> sortBuffer_;   // reused every frame, never reallocs
    std::vector<Entry> order_;
    int blendDraws_ = 0;
    int totalDraws_ = 0;
    GLState glState_;
};

} // namespace glass_water

#endif // GLASS_WATER_BLENDPASS_H
