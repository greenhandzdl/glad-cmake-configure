#ifndef GFX_RENDER_RENDERFRAME_H
#define GFX_RENDER_RENDERFRAME_H

/**
 * @file RenderFrame.h
 * @brief Per-frame bundle of everything the render passes need (plan "RenderPass").
 *
 * A plain aggregate of non-owning pointers + a few scalars. It owns nothing and
 * performs no GL; the application assembles it once per frame (after updating the
 * camera and scene) and hands it to Renderer::Render, which runs each pass in
 * order. Keeping the shared state in one struct (instead of threading many
 * arguments) is what lets a pass like GeometryPass stay a self-contained unit.
 */

#include <glm/glm.hpp>

#include "gfx/light/Light.h"     // LightSetup (value member)

namespace gfx {

class Camera;
class Frustum;
class Scene;
class SceneNode;
class PostProcessChain;
class LightBuffer;
class CascadedShadowMap;
class EnvironmentMap;
class ShaderProgram;
class SkyboxRenderer;
class DebugDraw;
class SpriteBatch;
class Font;
class Texture2D;
class Profiler;
class InstancedMesh;
class ParticleBatch;

// RenderFrame is split into a small *core* (the minimum that lets the scene
// draws: camera + frustum + scene + one lighting UBO + a program + a target)
// and a handful of *optional* subsystem records. Each optional block groups
// the pointers and the enable-flag of one feature; leaving it default-
// constructed means "the app did not bring that subsystem" and every consumer
// stays behind its null / enabled check. This is what makes skybox / shadow /
// IBL / bloom / particles / instances opt-in rather than mandatory parts of
// the frame contract.
struct RenderFrame {
    // --- core: camera + derived view state (owned by the application) ------
    const Camera*   camera   = nullptr;
    const Frustum*  frustum  = nullptr;
    Scene*          scene    = nullptr;
    glm::mat4       viewProj{1.0f};

    // --- core render subsystems the base geometry pass always needs --------
    // `post` is now OPTIONAL: null means the geometry / voxel passes render
    // straight to the default framebuffer and no PostProcessPass is added, so
    // a scene can be drawn without building the whole HDR/MSAA/bloom chain.
    PostProcessChain* post    = nullptr;
    LightBuffer*      lights  = nullptr;
    const ShaderProgram* pbr  = nullptr;
    LightSetup        lightSetup;      // CPU-side lighting snapshot the app applied

    // Interaction highlight (optional; null draws nothing extra).
    SceneNode* selected = nullptr;

    // --- optional subsystem records ---------------------------------------
    // Sky cube + the environment map that feeds both it and IBL. Default-empty
    // => no skybox pass content, no IBL source bound.
    struct Sky {
        SkyboxRenderer*     box = nullptr;
        const EnvironmentMap* env = nullptr;   // shared: sky face set + IBL source
    } sky;

    // Cascaded shadow map + the depth program that fills it.
    struct Shadow {
        CascadedShadowMap*  map = nullptr;
        const ShaderProgram* depth = nullptr;
        glm::vec3 sunToward{0.0f, 1.0f, 0.0f};
        bool      enabled = false;
    } shadow;

    // Image-based lighting toggle. The data lives in `sky.env`; this only says
    // whether the PBR shader should sample it (so IBL can stay off while the
    // skybox still draws).
    struct Ibl { bool enabled = false; } ibl;

    // GPU-instanced field, lit by the same LightingBlock UBO (optional demo).
    struct Instances {
        InstancedMesh*       field = nullptr;
        const ShaderProgram* prog  = nullptr;
        bool                 enabled = false;
    } instances;

    // World-space point particles, drawn by VoxelTransparentPass right after the
    // blended terrain so debris sits correctly against water (optional).
    ParticleBatch* particles = nullptr;

    // 2D / diagnostic overlays drawn by DebugHudPass.
    struct Overlay {
        DebugDraw*   debug   = nullptr;
        SpriteBatch* sprite  = nullptr;
        const Font*  font    = nullptr;
        const Texture2D* white = nullptr;
        Profiler*    profiler = nullptr;
        bool         useDebug = false;
    } overlay;

    // --- frame scalars / toggles ------------------------------------------
    bool useBloom = false;   // bloom rides on `post`; no separate record needed
    bool ortho    = false;   // projection mode: false=perspective, true=ortho

    int fbWidth  = 0;
    int fbHeight = 0;
    double smoothedFps = 0.0;

    // Outputs the passes fill for the HUD to read.
    int visibleCount = 0;
    int totalNodes   = 0;
};

} // namespace gfx

#endif // GFX_RENDER_RENDERFRAME_H
