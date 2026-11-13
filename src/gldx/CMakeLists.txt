# ============================================================================
# gldx  — the engine, delivered as a C++20 named module (static library)
# ============================================================================
# Every engine translation unit is a module-implementation unit of `gldx`; the
# single primary interface unit is gldx.cppm (declared via the CXX_MODULES
# FILE_SET below). stb / assimp / freetype stay private to the library: they are
# only ever textually included inside individual implementation units, never on
# the exported interface. GLAD + GLM leak into exported signatures so their
# include dirs propagate PUBLIC (importers reach the same global-module types).
#
# GLAD_TARGET / GLM_INCLUDE_DIR and the assimp + freetype options come from the
# root scope (cmake/Dependencies.cmake + Assimp.cmake + FreeType.cmake),
# inherited into here.

set(GLDX_SOURCES
    core/RenderContext.cpp
    core/GLBuffer.cpp
    core/VertexArray.cpp
    core/TransformFeedback.cpp
    core/UniformBuffer.cpp
    core/Sampler.cpp
    shader/ShaderProgram.cpp
    anim/Skeleton.cpp
    anim/SkinnedMesh.cpp
    anim/RigProfile.cpp
    anim/RigNaming.cpp
    anim/Pose.cpp
    anim/RiggedModel.cpp
    anim/GroundContact.cpp
    anim/ProceduralWalk.cpp
    anim/ProceduralJump.cpp
    anim/ProceduralRun.cpp
    anim/ProceduralCrouch.cpp
    anim/ProceduralAttack.cpp
    anim/AnimStateMachine.cpp
    geometry/Mesh.cpp
    geometry/GeometryFactory.cpp
    geometry/InstancedMesh.cpp
    texture/Texture2D.cpp
    texture/TextureCubeMap.cpp
    texture/RenderTexture.cpp
    camera/Camera.cpp
    camera/Frustum.cpp
    texture/Texture2DArray.cpp
    util/Noise.cpp
    util/Screenshot.cpp
    voxel/ChunkMesher.cpp
    voxel/VoxelMeshGpu.cpp
    render/ParticleBatch.cpp
    scene/SceneNode.cpp
    scene/Scene.cpp
    render/Renderer.cpp
    render/RenderPasses.cpp
    render/Framebuffer.cpp
    render/PostProcessChain.cpp
    render/SkyboxRenderer.cpp
    render/SpriteBatch.cpp
    text/Font.cpp
    text/Hud.cpp
    light/EnvironmentMap.cpp
    shadow/CascadedShadowMap.cpp
    debug/DebugDraw.cpp
    debug/Profiler.cpp
    assets/ThreadPool.cpp
    assets/AssetManager.cpp
    assets/ModelLoader.cpp
    assets/ImageLoader.cpp
    third_party/stb_image_impl.cpp   # plain (non-module) 3rd-party impl TU
    util/ScreenshotHook.cpp)         # plain (non-module) GMF-symbol TU

add_library(gldx STATIC ${GLDX_SOURCES})

target_sources(gldx PUBLIC FILE_SET CXX_MODULES FILES gldx.cppm)

target_include_directories(gldx PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${GLM_INCLUDE_DIR})

target_link_libraries(gldx PUBLIC
    glfw
    ${GLAD_TARGET})

target_link_libraries(gldx PRIVATE
    stb)
if(GLDX_ENABLE_ASSIMP)
    target_link_libraries(gldx PRIVATE
        assimp::assimp)
else()
    target_compile_definitions(gldx PRIVATE GLDX_NO_ASSIMP)
endif()
if(GLDX_ENABLE_FREETYPE)
    target_link_libraries(gldx PRIVATE
        Freetype::Freetype)
else()
    target_compile_definitions(gldx PRIVATE GLDX_NO_FREETYPE)
endif()

# Per-compiler module tweaks. CMake >= 3.28 drives named-module compilation and
# the import-graph scan for all three vendors, so this only patches up details:
#   * MSVC: /bigobj as cheap insurance against C1128 (a module TU can exceed the
#     default 65,279 addressable COFF sections). /bigobj does *not* address
#     C1605 - see the note in src/demo/CMakeLists.txt and the failure-time probe
#     in .github/workflows/release.yml.
#   * MSVC: /utf-8 is PUBLIC, and that is load-bearing, not hygiene. It defines
#     `_UTF8` on the module's command line, and MSVC bakes that macro set into
#     the .ifc: an importer whose macro environment differs is not compatible
#     with the interface, and MSVC says so once per import statement with
#     C5050 ("_UTF8 is defined in module command line and not in current
#     command line"). Every importer therefore has to see the identical
#     environment - not `_UTF8` itself, but the *preprocessor state* the module
#     was built under. Leaving it PRIVATE put the three libraries on one side
#     and all 30-odd demo/executable TUs on the other, which is where the
#     v1.7.0 Windows failures came from: the importer's own `#include
#     <glm/glm.hpp>` (glm is attached to the global module by gmf.hpp) then
#     re-parsed headers whose global-module entities had already been imported,
#     and MSVC reported the whole tree as redefinitions - C2011
#     'glm::qualifier', C2572/C2953, and the same for <vector> - instead of
#     treating them as the same types. That is a *cross-platform* inconsistency:
#     clang happened to tolerate the mismatch, so only the Windows job died.
#     Exporting /utf-8 makes the importer environment match by construction, and
#     it doubles as the source-encoding rule the demos' non-ASCII comments need.
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(gldx
        PUBLIC  /utf-8
        PRIVATE /bigobj)
endif()
