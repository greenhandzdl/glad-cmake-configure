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

struct RenderFrame {
    // Camera + derived view state (owned by the application).
    const Camera*   camera   = nullptr;
    const Frustum*  frustum  = nullptr;
    Scene*          scene    = nullptr;
    glm::mat4       viewProj{1.0f};

    // Render targets / subsystems (owned by the application).
    PostProcessChain*   post    = nullptr;
    LightBuffer*        lights  = nullptr;
    CascadedShadowMap*  shadow  = nullptr;
    EnvironmentMap*     env     = nullptr;
    SkyboxRenderer*     skybox  = nullptr;
    const ShaderProgram* pbr    = nullptr;
    const ShaderProgram* depth  = nullptr;

    // Lighting inputs for this frame.
    LightSetup lightSetup;
    glm::vec3  sunToward{0.0f, 1.0f, 0.0f};

    // 2D / diagnostic overlays.
    DebugDraw*   debug   = nullptr;
    SpriteBatch* sprite  = nullptr;
    const Font*  font    = nullptr;
    const Texture2D* white = nullptr;
    Profiler*    profiler = nullptr;

    // GPU-instanced field (optional demo).
    InstancedMesh*       instField = nullptr;
    const ShaderProgram* instProg  = nullptr;

    // Feature toggles + interaction state.
    bool useShadow   = false;
    bool useIbl      = false;
    bool useBloom    = false;
    bool useDebug    = false;
    bool useInstances = false;
    bool ortho       = false;   // projection mode: false=perspective, true=ortho
    SceneNode* selected = nullptr;

    int   fbWidth  = 0;
    int   fbHeight = 0;
    double smoothedFps = 0.0;

    // Outputs the passes fill for the HUD to read.
    int visibleCount = 0;
    int totalNodes   = 0;
};

} // namespace gfx

#endif // GFX_RENDER_RENDERFRAME_H
