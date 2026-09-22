// ============================================================================
// gldx - C++20 named module (primary interface unit)
//
// The whole engine is delivered as a single module `gldx`. Consumers (main.cpp)
// only need `import gldx;` to reach every public type/free function; they no
// longer `#include` individual gldx headers.
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
// declaration in module gldx"). main.cpp reaches the very same global-module
// glad/glm types by textually including Platform.h/glm alongside `import gldx;`.
//
// Deliberately NOT exported: <GLFW/glfw3.h> and the app constants live in
// gldx/core/Platform.h, which stays a plain text-include header (only the
// executable needs a windowing context). stb / assimp are implementation
// details kept inside individual module-implementation units and never surface
// on this interface.
// ============================================================================

module;

// Shared global-module-fragment includes (GLAD, GLM, common std) — the same set
// every `module gldx;` implementation unit pulls in, so all these entities attach
// to the global module consistently across the whole library and main.cpp.
#include "gldx/gmf.hpp"

export module gldx;

export {
// --- core ------------------------------------------------------------------
#include "gldx/core/RenderContext.h"
#include "gldx/core/GLBuffer.h"
#include "gldx/core/VertexArray.h"
#include "gldx/core/TransformFeedback.h"
#include "gldx/core/UniformBuffer.h"
#include "gldx/core/Sampler.h"

// --- shader ----------------------------------------------------------------
#include "gldx/shader/ShaderProgram.h"
#include "gldx/shader/ShaderLib.h"
#include "gldx/shader/DebugShaders.h"
#include "gldx/shader/IblShaders.h"
#include "gldx/shader/PostProcessShaders.h"
#include "gldx/shader/SpriteShaders.h"
#include "gldx/shader/ParticleShaders.h"
#include "gldx/shader/VoxelShaders.h"

// --- geometry --------------------------------------------------------------
#include "gldx/geometry/Vertex.h"
#include "gldx/geometry/Mesh.h"
#include "gldx/geometry/InstancedMesh.h"
#include "gldx/geometry/Model.h"
#include "gldx/geometry/GeometryFactory.h"

// --- texture ---------------------------------------------------------------
#include "gldx/texture/Texture2D.h"
#include "gldx/texture/Texture2DArray.h"
#include "gldx/texture/TextureCubeMap.h"
#include "gldx/texture/RenderTexture.h"

// --- camera ----------------------------------------------------------------
#include "gldx/camera/Camera.h"
#include "gldx/camera/Frustum.h"
#include "gldx/camera/Picking.h"
#include "gldx/camera/VoxelRay.h"

// --- light / shadow --------------------------------------------------------
#include "gldx/light/Light.h"
#include "gldx/light/LightBuffer.h"
#include "gldx/light/EnvironmentMap.h"
#include "gldx/shadow/ShadowData.h"
#include "gldx/shadow/CascadedShadowMap.h"

// --- material --------------------------------------------------------------
#include "gldx/material/Material.h"

// --- scene -----------------------------------------------------------------
#include "gldx/scene/Transform.h"
#include "gldx/scene/SceneNode.h"
#include "gldx/scene/Scene.h"

// --- render ----------------------------------------------------------------
#include "gldx/render/Framebuffer.h"
#include "gldx/render/RenderFrame.h"
#include "gldx/render/RenderPass.h"
#include "gldx/render/RenderPasses.h"
#include "gldx/render/Renderer.h"
#include "gldx/render/PostProcessChain.h"
#include "gldx/render/SkyboxRenderer.h"
#include "gldx/render/SpriteBatch.h"
#include "gldx/render/ParticleBatch.h"

// --- text ------------------------------------------------------------------
#include "gldx/text/Font.h"
#include "gldx/text/TextRenderer.h"

// --- debug -----------------------------------------------------------------
#include "gldx/debug/DebugDraw.h"
#include "gldx/debug/Profiler.h"

// --- assets ----------------------------------------------------------------
#include "gldx/assets/ThreadPool.h"
#include "gldx/assets/ImageLoader.h"
#include "gldx/assets/ModelLoader.h"
#include "gldx/assets/AssetManager.h"

// --- util ------------------------------------------------------------------
#include "gldx/util/Noise.h"
#include "gldx/util/Screenshot.h"

// --- voxel -----------------------------------------------------------------
// Chunk storage + meshing are pure CPU data types; the GPU records and the
// render passes that consume them sit above (render/RenderPasses.h).
#include "gldx/voxel/BlockRegistry.h"
#include "gldx/voxel/Chunk.h"
#include "gldx/voxel/ChunkMesher.h"
#include "gldx/voxel/Collision.h"
#include "gldx/voxel/VoxelMeshGpu.h"
}
