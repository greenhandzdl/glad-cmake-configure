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
// executable needs a windowing context). stb / assimp / freetype are
// implementation details kept inside individual module-implementation units and
// never surface on this interface: consumers see gldx::Font and gldx::Hud, and
// which rasteriser filled the atlas is a build choice, not an API one.
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

// --- animation -------------------------------------------------------------
// Skinning is split like every other asset: the CPU side (Skeleton, the
// SkinnedVertex layout, ModelLoader::LoadSkinned) builds on a worker thread and
// the GPU side (SkinnedMesh, RiggedModel) is uploaded on the render thread.
//
// On top of that split sit the four pieces a *user* of a rig needs, so a demo
// only has to author motion instead of re-discovering the plumbing:
//   - RigSlot        the semantic joints a rig user asks about, name-agnostic
//   - RigNaming      one bone-naming *family* per exporter (Mixamo, Biped, UE);
//                    an abstract class so a new family is a subclass, not more
//                    strings in a table
//   - RigProfile     resolves slots against a Skeleton via a RigNaming, and
//                    measures which way is up (never assumed)
//   - Pose           "rotate this bone about a world axis", plus rest == bind
//   - GroundContact  solve the pelvis height from where the soles actually are
//   - RiggedModel    upload + own one rig, including the 1x1 stand-in maps that
//                    keep every sampler backed (Apple drops a draw otherwise)
//   - AnimPoseSource the interface every action implements ("what does this rig
//                    look like at time t"). ProceduralWalk / ProceduralJump are
//                    the two built-in answers; a keyframe clip loader would be a
//                    third, and nothing above this line would have to change.
//   - AnimStateMachine  which action is playing, and how to get from one to the
//                    other without a pop. Owns no action - it only references
//                    them, so a caller's tuning stays the caller's.
#include "gldx/anim/Skeleton.h"
#include "gldx/anim/SkinnedVertex.h"
#include "gldx/anim/SkinnedMesh.h"
#include "gldx/anim/RigSlot.h"
#include "gldx/anim/RigNaming.h"
#include "gldx/anim/RigProfile.h"
#include "gldx/anim/Pose.h"
#include "gldx/anim/AnimPoseSource.h"
#include "gldx/anim/ProceduralWalk.h"
#include "gldx/anim/ProceduralJump.h"
#include "gldx/anim/AnimStateMachine.h"
#include "gldx/anim/GroundContact.h"
#include "gldx/anim/RiggedModel.h"

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
#include "gldx/text/Hud.h"

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
