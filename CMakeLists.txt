cmake_minimum_required(VERSION 3.16)
project(GLFW_Template VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# ============================================================================
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

# ============================================================================
# Executable
# ============================================================================
# The application only calls GLFW (shared) and GLAD (static, loads GL at
# runtime), so no direct OpenGL / X11 / Cocoa linking is required here — those
# dependencies are carried transitively by the GLFW and GLAD targets.
set(TARGET_NAME "GLFW_Template")

set(GFX_SOURCES
    src/gfx/core/RenderContext.cpp
    src/gfx/core/GLBuffer.cpp
    src/gfx/core/VertexArray.cpp
    src/gfx/core/UniformBuffer.cpp
    src/gfx/core/Sampler.cpp
    src/gfx/shader/ShaderProgram.cpp
    src/gfx/geometry/Mesh.cpp
    src/gfx/geometry/GeometryFactory.cpp
    src/gfx/texture/Texture2D.cpp
    src/gfx/texture/TextureCubeMap.cpp
    src/gfx/texture/RenderTexture.cpp
    src/gfx/camera/Camera.cpp
    src/gfx/render/Renderer.cpp
    src/gfx/render/Framebuffer.cpp
    src/gfx/render/SkyboxRenderer.cpp
    src/gfx/light/EnvironmentMap.cpp
    src/gfx/shadow/CascadedShadowMap.cpp
    src/gfx/assets/ThreadPool.cpp
    src/gfx/assets/AssetManager.cpp
    src/gfx/assets/ModelLoader.cpp)

add_executable(${TARGET_NAME}
    src/main.cpp
    src/utils/utils.cpp
    src/utils/Shader.cpp
    src/utils/stb.cpp
    ${GFX_SOURCES})

target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/headers
    ${CMAKE_SOURCE_DIR}/src
    ${GLM_INCLUDE_DIR})

target_link_libraries(${TARGET_NAME} PRIVATE
    glfw
    ${GLAD_TARGET}
    stb
    assimp::assimp)

set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME ${TARGET_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

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
message(STATUS "  Output      : ${CMAKE_SOURCE_DIR}/output")
message(STATUS "")
