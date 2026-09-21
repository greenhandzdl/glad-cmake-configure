# ============================================================================
# gldx  — the engine, delivered as a C++20 named module (static library)
# ============================================================================
# Every engine translation unit is a module-implementation unit of `gldx`; the
# single primary interface unit is gldx.cppm (declared via the CXX_MODULES
# FILE_SET below). stb / assimp stay private to the library: they are only ever
# textually included inside individual implementation units, never on the
# exported interface. GLAD + GLM leak into exported signatures so their include
# dirs propagate PUBLIC (importers reach the same global-module types).
#
# GLAD_TARGET / GLM_INCLUDE_DIR / the assimp option come from the root scope
# (cmake/Dependencies.cmake + cmake/Assimp.cmake), inherited into here.

set(GLDX_SOURCES
    core/RenderContext.cpp
    core/GLBuffer.cpp
    core/VertexArray.cpp
    core/UniformBuffer.cpp
    core/Sampler.cpp
    shader/ShaderProgram.cpp
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
    light/EnvironmentMap.cpp
    shadow/CascadedShadowMap.cpp
    debug/DebugDraw.cpp
    debug/Profiler.cpp
    assets/ThreadPool.cpp
    assets/AssetManager.cpp
    assets/ModelLoader.cpp
    assets/ImageLoader.cpp
    third_party/stb_image_impl.cpp)   # plain (non-module) 3rd-party impl TU

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

# Per-compiler module tweaks. CMake >= 3.28 drives named-module compilation and
# the import-graph scan for all three vendors, so this only patches up details:
#   * MSVC: /utf-8 so the embedded GLSL raw strings + any non-ASCII comments are
#     read as UTF-8 regardless of the system code page.
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(gldx PRIVATE /utf-8)
endif()
