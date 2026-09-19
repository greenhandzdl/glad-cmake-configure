// ============================================================================
// gfx - C++20 named module (primary interface unit)
//
// The whole engine is delivered as a single module `gfx`. Consumers (main.cpp)
// only need `import gfx;` to reach every public type/free function; they no
// longer `#include` individual gfx headers.
//
// Why a single interface unit rather than 13 partitions: the engine is a
// header-centric codebase (each class owns a guarded .h). Wrapping those
// headers once in `export { #include ... }` gives the same import surface
// with far less duplication and no partition-ordering hazards, which is the
// pragmatic path for cross-vendor named modules (see plan section 2/5).
//
// Global module fragment: every third-party header that leaks into an exported
// signature (GLAD's GLenum/GLuint, GLM's vec3/mat4) plus the full transitive
// std set the engine headers pull must be textually included here FIRST. That
// attaches those entities to the *global module*, so the exported declarations
// reference global-module types instead of re-declaring them inside the module
// purview (which would trigger "declaration of X in the global module follows
// declaration in module gfx"). main.cpp reaches the very same global-module
// glad/glm types by textually including Platform.h/glm alongside `import gfx;`.
//
// Deliberately NOT exported: <GLFW/glfw3.h> and the app constants live in
// gfx/core/Platform.h, which stays a plain text-include header (only the
// executable needs a windowing context). stb / assimp are implementation
// details kept inside individual module-implementation units and never surface
// on this interface.
// ============================================================================

module;

// Shared global-module-fragment includes (GLAD, GLM, common std) — the same set
// every `module gfx;` implementation unit pulls in, so all these entities attach
// to the global module consistently across the whole library and main.cpp.
#include "gfx/gmf.hpp"

export module gfx;

export {
// --- core ------------------------------------------------------------------
#include "gfx/core/RenderContext.h"
#include "gfx/core/GLBuffer.h"
#include "gfx/core/VertexArray.h"
#include "gfx/core/UniformBuffer.h"
#include "gfx/core/Sampler.h"

// --- shader ----------------------------------------------------------------
#include "gfx/shader/ShaderProgram.h"
#include "gfx/shader/ShaderLib.h"
#include "gfx/shader/DebugShaders.h"
#include "gfx/shader/IblShaders.h"
#include "gfx/shader/PostProcessShaders.h"
#include "gfx/shader/SpriteShaders.h"

// --- geometry --------------------------------------------------------------
#include "gfx/geometry/Vertex.h"
#include "gfx/geometry/Mesh.h"
#include "gfx/geometry/InstancedMesh.h"
#include "gfx/geometry/Model.h"
#include "gfx/geometry/GeometryFactory.h"

// --- texture ---------------------------------------------------------------
#include "gfx/texture/Texture2D.h"
#include "gfx/texture/TextureCubeMap.h"
#include "gfx/texture/RenderTexture.h"

// --- camera ----------------------------------------------------------------
#include "gfx/camera/Camera.h"
#include "gfx/camera/Frustum.h"
#include "gfx/camera/Picking.h"

// --- light / shadow --------------------------------------------------------
#include "gfx/light/Light.h"
#include "gfx/light/LightBuffer.h"
#include "gfx/light/EnvironmentMap.h"
#include "gfx/shadow/ShadowData.h"
#include "gfx/shadow/CascadedShadowMap.h"

// --- material --------------------------------------------------------------
#include "gfx/material/Material.h"

// --- scene -----------------------------------------------------------------
#include "gfx/scene/Transform.h"
#include "gfx/scene/SceneNode.h"
#include "gfx/scene/Scene.h"

// --- render ----------------------------------------------------------------
#include "gfx/render/Framebuffer.h"
#include "gfx/render/RenderFrame.h"
#include "gfx/render/RenderPass.h"
#include "gfx/render/RenderPasses.h"
#include "gfx/render/Renderer.h"
#include "gfx/render/PostProcessChain.h"
#include "gfx/render/SkyboxRenderer.h"
#include "gfx/render/SpriteBatch.h"

// --- text ------------------------------------------------------------------
#include "gfx/text/Font.h"
#include "gfx/text/TextRenderer.h"

// --- debug -----------------------------------------------------------------
#include "gfx/debug/DebugDraw.h"
#include "gfx/debug/Profiler.h"

// --- assets ----------------------------------------------------------------
#include "gfx/assets/ThreadPool.h"
#include "gfx/assets/ImageLoader.h"
#include "gfx/assets/ModelLoader.h"
#include "gfx/assets/AssetManager.h"
}
