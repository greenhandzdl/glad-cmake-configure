cmake_minimum_required(VERSION 3.28)
project(GLFW_Template VERSION 1.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C++20 named modules: with CMake >= 3.28 + a recent toolchain the import-graph
# scanner is wired up automatically for FILE_SET CXX_MODULES sources.

# All runtime artifacts land in <src>/output (single-config friendly, used by CI).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

# ============================================================================
# GLFW  (Homebrew / apt / vcpkg all export an imported target `glfw`)
# ============================================================================
find_package(glfw3 QUIET)
if(NOT TARGET glfw)
    message(FATAL_ERROR
        "GLFW3 not found. Install it first:\n"
        "  macOS: brew install glfw\n"
        "  Linux: sudo apt-get install libglfw3-dev\n"
        "  Windows: vcpkg install glfw3")
endif()

# ============================================================================
# GLAD  (OpenGL loader)
#   1) prefer a system package (find_package(glad CONFIG) -> glad::glad)
#   2) otherwise generate bindings from the third_party/glad submodule
#      (glad2 generator: Python 3 + jinja2; falls back to an isolated uv venv)
# ============================================================================
find_package(glad CONFIG QUIET)
if(TARGET glad::glad)
    message(STATUS "GLAD: system package (glad::glad)")
    set(GLAD_TARGET glad::glad)
    set(GLAD_SOURCE "system")
else()
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/glad/glad/__main__.py)
        message(FATAL_ERROR
            "GLAD submodule not found. Initialize it first:\n"
            "  git submodule update --init --recursive")
    endif()

    find_package(Python COMPONENTS Interpreter REQUIRED)

    # The glad2 generator needs jinja2. Use the system Python when it already
    # provides jinja2; otherwise create an isolated uv venv (keeps the global
    # interpreter clean and sidesteps PEP 668).
    set(GLAD_GEN_PYTHON ${Python_EXECUTABLE})
    execute_process(
        COMMAND ${Python_EXECUTABLE} -c "import jinja2"
        RESULT_VARIABLE GLAD_JINJA2_RC OUTPUT_QUIET ERROR_QUIET)
    if(NOT GLAD_JINJA2_RC EQUAL 0)
        find_program(UV_EXECUTABLE uv)
        if(NOT UV_EXECUTABLE)
            message(FATAL_ERROR
                "System Python has no jinja2 and 'uv' was not found.\n"
                "  Install uv (https://docs.astral.sh/uv/) or jinja2 (pip install jinja2)")
        endif()
        set(GLAD_VENV_DIR ${CMAKE_CURRENT_BINARY_DIR}/glad_venv)
        if(WIN32)
            set(GLAD_VENV_PYTHON ${GLAD_VENV_DIR}/Scripts/python.exe)
        else()
            set(GLAD_VENV_PYTHON ${GLAD_VENV_DIR}/bin/python)
        endif()
        if(NOT EXISTS ${GLAD_VENV_PYTHON})
            message(STATUS "GLAD: creating uv venv for the generator (${GLAD_VENV_DIR})")
            execute_process(
                COMMAND ${UV_EXECUTABLE} venv ${GLAD_VENV_DIR}
                RESULT_VARIABLE GLAD_VENV_RC)
            if(NOT GLAD_VENV_RC EQUAL 0)
                message(FATAL_ERROR "Failed to create uv venv: ${UV_EXECUTABLE} venv ${GLAD_VENV_DIR}")
            endif()
            execute_process(
                COMMAND ${UV_EXECUTABLE} pip install --python ${GLAD_VENV_PYTHON} jinja2
                RESULT_VARIABLE GLAD_PIP_RC)
            if(NOT GLAD_PIP_RC EQUAL 0)
                message(FATAL_ERROR "Failed to install jinja2 into the GLAD venv")
            endif()
        endif()
        set(GLAD_GEN_PYTHON ${GLAD_VENV_PYTHON})
    endif()

    set(GLAD_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gladsources/glad_gl_core_41)
    add_custom_command(
        OUTPUT ${GLAD_GEN_DIR}/src/gl.c ${GLAD_GEN_DIR}/include/glad/gl.h ${GLAD_GEN_DIR}/include/KHR/khrplatform.h
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${GLAD_GEN_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GLAD_GEN_DIR}
        COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${CMAKE_SOURCE_DIR}/third_party/glad"
            ${GLAD_GEN_PYTHON} -m glad --out-path ${GLAD_GEN_DIR} --api gl:core=4.1 --reproducible c
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${CMAKE_SOURCE_DIR}/third_party/glad/glad/__main__.py
        COMMENT "Generating GLAD from submodule (OpenGL 4.1 Core)"
        VERBATIM)

    add_library(glad_gl_core_41 STATIC ${GLAD_GEN_DIR}/src/gl.c)
    target_include_directories(glad_gl_core_41 PUBLIC ${GLAD_GEN_DIR}/include)
    target_link_libraries(glad_gl_core_41 PUBLIC ${CMAKE_DL_LIBS})

    set(GLAD_TARGET glad_gl_core_41)
    set(GLAD_SOURCE "submodule (OpenGL 4.1 Core)")
endif()

# ============================================================================
# GLM (header-only)  &  STB (header-only submodule)
# ============================================================================
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    HINTS ${BREW_PREFIX}/include /usr/local/include /usr/include
          ${CMAKE_SOURCE_DIR}/third_party)
if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR
        "GLM not found. Install it first:\n"
        "  macOS: brew install glm\n"
        "  Linux: sudo apt-get install libglm-dev\n"
        "  Windows: vcpkg install glm")
endif()

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h)
    message(FATAL_ERROR
        "STB submodule not found. Initialize it first:\n"
        "  git submodule update --init --recursive")
endif()
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_SOURCE_DIR}/third_party/stb)

# ============================================================================
# Assimp (3D model import library) — vendored as a git submodule and built
# from source as a static subproject. This keeps the build self-contained
# across all three CI platforms (no system/vcpkg assimp package needed).
#
# GLDX_ENABLE_ASSIMP=OFF drops the entire model-import path (and this heavy
# dependency) out of the build: the submodule is never checked or configured,
# gldx links without assimp, and ModelLoader::Load reports unavailability at
# runtime. The exported `gldx` module interface is unchanged either way.
# ============================================================================
option(GLDX_ENABLE_ASSIMP "Build assimp-backed 3D model import" ON)

if(GLDX_ENABLE_ASSIMP)
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/assimp/CMakeLists.txt)
    message(FATAL_ERROR
        "Assimp submodule not found. Initialize it first:\n"
        "  git submodule update --init --recursive")
endif()

# Configure Assimp before entering its subtree: library-only static build,
# no command-line tools / tests, never let upstream warnings become errors,
# and ASSIMP_INSTALL=OFF keeps its export rules out of our build since we
# only consume the library target. We build Assimp's BUNDLED zlib so its
# minizip/unzip sources (needed by the FBX importer) are wired up too.
set(BUILD_SHARED_LIBS OFF                       CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF               CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF                      CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON                         CACHE BOOL "" FORCE)  # importers only
set(ASSIMP_WARNINGS_AS_ERRORS OFF               CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF             CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON                        CACHE BOOL "" FORCE)  # bundled zlib + minizip
set(ASSIMP_HUNTER_ENABLED OFF                   CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF                          CACHE BOOL "" FORCE)
set(GENERATE_DOC OFF                            CACHE BOOL "" FORCE)

add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/assimp EXCLUDE_FROM_ALL)

# Assimp's bundled zlib 1.2.x has a legacy `#if defined(MACOS)||defined(TARGET_OS_MAC)`
# branch that #defines fdopen(...) NULL, clashing with the modern macOS SDK's
# <stdio.h> (which transitively defines TARGET_OS_MAC). Defining fdopen to itself
# makes the `#ifndef fdopen` guard skip that bogus macro without altering calls.
if(APPLE)
    foreach(_zlib_target zlib zlibstatic)
        if(TARGET ${_zlib_target})
            target_compile_definitions(${_zlib_target} PRIVATE fdopen=fdopen)
        endif()
    endforeach()
endif()
message(STATUS "Assimp: submodule (static, bundled zlib)")
set(ASSIMP_SOURCE "submodule (static, bundled zlib)")

# Newer standard libraries (libc++ >= 19, recent MSVC STL) no longer transitively
# pull <ostream>/<cstdint>/... into every header, which breaks a few legacy Assimp
# contrib sources (e.g. poly2tri shapes.h uses std::ostream without including it)
# once the whole tree is compiled by a module-capable toolchain. Force-include the
# handful of headers they omit — applied only to the vendored Assimp target so our
# own sources keep honest, explicit includes. (A submodule file edit is not an
# option: CI checks it out clean.)
if(TARGET assimp)
    set(_assimp_force_includes ostream cstdio cstdlib cstring cstdint cstddef string vector memory utility array algorithm numeric)
    foreach(_h ${_assimp_force_includes})
        target_compile_options(assimp PRIVATE
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CXX_COMPILER_ID:MSVC>>:/FI${_h}>
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-include${_h}>)
    endforeach()
endif()
else()
    message(STATUS "Assimp: DISABLED (GLDX_ENABLE_ASSIMP=OFF) - model import unavailable")
    set(ASSIMP_SOURCE "DISABLED (GLDX_ENABLE_ASSIMP=OFF)")
endif()

# ============================================================================
# gldx  — the engine, delivered as a C++20 named module (static library)
# ============================================================================
# Every engine translation unit is a module-implementation unit of `gldx`; the
# single primary interface unit is src/gldx/gldx.cppm (declared via the CXX_MODULES
# FILE_SET below). stb / assimp stay private to the library: they are only ever
# textually included inside individual implementation units, never on the
# exported interface. GLAD + GLM leak into exported signatures so their include
# dirs propagate PUBLIC (main.cpp reaches the same global-module types).
set(TARGET_NAME "GLFW_Template")

set(GLDX_SOURCES
    src/gldx/core/RenderContext.cpp
    src/gldx/core/GLBuffer.cpp
    src/gldx/core/VertexArray.cpp
    src/gldx/core/UniformBuffer.cpp
    src/gldx/core/Sampler.cpp
    src/gldx/shader/ShaderProgram.cpp
    src/gldx/geometry/Mesh.cpp
    src/gldx/geometry/GeometryFactory.cpp
    src/gldx/geometry/InstancedMesh.cpp
    src/gldx/texture/Texture2D.cpp
    src/gldx/texture/TextureCubeMap.cpp
    src/gldx/texture/RenderTexture.cpp
    src/gldx/camera/Camera.cpp
    src/gldx/camera/Frustum.cpp
    src/gldx/texture/Texture2DArray.cpp
    src/gldx/util/Noise.cpp
    src/gldx/voxel/ChunkMesher.cpp
    src/gldx/voxel/VoxelMeshGpu.cpp
    src/gldx/render/ParticleBatch.cpp
    src/gldx/scene/SceneNode.cpp
    src/gldx/scene/Scene.cpp
    src/gldx/render/Renderer.cpp
    src/gldx/render/RenderPasses.cpp
    src/gldx/render/Framebuffer.cpp
    src/gldx/render/PostProcessChain.cpp
    src/gldx/render/SkyboxRenderer.cpp
    src/gldx/render/SpriteBatch.cpp
    src/gldx/text/Font.cpp
    src/gldx/light/EnvironmentMap.cpp
    src/gldx/shadow/CascadedShadowMap.cpp
    src/gldx/debug/DebugDraw.cpp
    src/gldx/debug/Profiler.cpp
    src/gldx/assets/ThreadPool.cpp
    src/gldx/assets/AssetManager.cpp
    src/gldx/assets/ModelLoader.cpp
    src/gldx/assets/ImageLoader.cpp
    src/gldx/third_party/stb_image_impl.cpp)   # plain (non-module) 3rd-party impl TU

add_library(gldx STATIC ${GLDX_SOURCES})

target_sources(gldx PUBLIC FILE_SET CXX_MODULES FILES src/gldx/gldx.cppm)

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

# ============================================================================
# gldxcli — the demo-side command-line switch parser, as a C++20 named module
# ============================================================================
# Formerly the header-only src/demo/demo_cli.h. It is deliberately independent
# of both the engine (gldx) and the window library (gldxwin): a pure standard-
# library argument parser, so it links nothing and only needs the src/ include
# dir to be reachable by importers. The primary interface unit holds Flags
# fully inline; gldxcli.cpp is a module implementation unit that gives the
# static archive a concrete object alongside the FILE_SET interface.
add_library(gldxcli STATIC src/gldxcli/gldxcli.cpp)

target_sources(gldxcli PUBLIC FILE_SET CXX_MODULES FILES src/gldxcli/gldxcli.cppm)

target_include_directories(gldxcli PUBLIC ${CMAKE_SOURCE_DIR}/src)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(gldxcli PRIVATE /utf-8)
endif()

# ============================================================================
# gldxwin — engine-agnostic GLFW windowing + frame loop, as a C++20 named module
# ============================================================================
# Replaces the lifecycle half of the old src/demo/demo_app.h: an App singleton
# (glfwInit/Terminate + GL 4.1 core hints) driving any number of Window objects,
# each with OnCreate/OnFrame/OnDestroy hooks and a user-data slot. It links only
# glfw + GLAD and imports no gldx type, so a demo owns its gldx::Renderer through
# the Window rather than the library reaching into the engine. glad/glfw attach to
# the global module via gmf.hpp; importers textually include gldx/core/Platform.h
# to call glfw* themselves.
add_library(gldxwin STATIC src/gldxwin/App.cpp src/gldxwin/Window.cpp)

target_sources(gldxwin PUBLIC FILE_SET CXX_MODULES FILES src/gldxwin/gldxwin.cppm)

target_include_directories(gldxwin PUBLIC ${CMAKE_SOURCE_DIR}/src)

target_link_libraries(gldxwin PUBLIC
    glfw
    ${GLAD_TARGET})

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(gldxwin PRIVATE /utf-8)
endif()

# ============================================================================
# Executables
# ============================================================================
# The applications only call GLFW (shared) and GLAD (static, loads GL at
# runtime), so no direct OpenGL / X11 / Cocoa linking is required here — those
# dependencies are carried transitively by the GLFW and GLAD targets, which the
# gldx library now links PUBLIC. Each demo is a sole consumer of `import gldx;`.
#
#   GLFW_Template  the minimum-pipeline hello-triangle (src/main.cpp, this file)
#   src/demo/*     the feature demos, one executable each, collected by the
#                  nested add_subdirectory below (see src/demo/CMakeLists.txt).
#
# The primary target keeps its historic name so the CI artifact
# GLFW_Template-<platform> is unaffected. It is defined here rather than under
# src/demo/ so the "what does the engine need to draw anything at all" answer
# stays at the top of the tree.
add_executable(${TARGET_NAME} src/main.cpp)

target_link_libraries(${TARGET_NAME} PRIVATE gldx gldxwin gldxcli)

set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME ${TARGET_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

# Every feature demo under src/demo/ is built automatically: add a
# src/demo/{feature}/main.cpp and it becomes a target, no CMake edit needed.
add_subdirectory(src/demo)

# ============================================================================
# Generate helper scripts from templates
# ============================================================================
set(BUILD_DIR_NAME "build")
set(OUTPUT_DIR_NAME "output")

foreach(script build clean run)
    configure_file(
        ${CMAKE_SOURCE_DIR}/scripts/${script}.sh.in
        ${CMAKE_SOURCE_DIR}/scripts/${script}.sh
        @ONLY)
    if(NOT WIN32)
        execute_process(COMMAND chmod +x ${CMAKE_SOURCE_DIR}/scripts/${script}.sh)
    endif()
endforeach()

# Remove every generated artifact (build tree, output, generated scripts).
add_custom_target(clean-project
    COMMENT "Cleaning build/, output/ and generated scripts..."
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/build
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_COMMAND} -E remove -f
        ${CMAKE_SOURCE_DIR}/scripts/build.sh
        ${CMAKE_SOURCE_DIR}/scripts/clean.sh
        ${CMAKE_SOURCE_DIR}/scripts/run.sh
    COMMAND ${CMAKE_COMMAND} -E echo "Clean complete!")

# ============================================================================
# Summary
# ============================================================================
message(STATUS "")
message(STATUS "${PROJECT_NAME} ${PROJECT_VERSION} configuration:")
message(STATUS "  Platform    : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  GLFW        : glfw")
message(STATUS "  GLAD        : ${GLAD_SOURCE}")
message(STATUS "  GLM         : ${GLM_INCLUDE_DIR}")
message(STATUS "  STB         : submodule")
message(STATUS "  Assimp      : ${ASSIMP_SOURCE}")
message(STATUS "  Output      : ${CMAKE_SOURCE_DIR}/output")
message(STATUS "")
