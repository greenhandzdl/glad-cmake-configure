cmake_minimum_required(VERSION 3.28)
project(GLFW_Template VERSION 1.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C++20 named modules: with CMake >= 3.28 + a recent toolchain the import-graph
# scanner is wired up automatically for FILE_SET CXX_MODULES sources.

# All runtime artifacts land in <src>/output (single-config friendly, used by CI).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

# Primary executable name, kept stable so the CI artifact GLFW_Template-<platform>
# is unaffected. Consumed by src/CMakeLists.txt.
set(TARGET_NAME "GLFW_Template")

# Build option: assimp-backed 3D model import (pulls the heavy vendored submodule
# into the build). Declared here so cmake/Assimp.cmake and src/gldx can both read
# it; OFF drops the whole model-import path without changing the gldx interface.
option(GLDX_ENABLE_ASSIMP "Build assimp-backed 3D model import" ON)

# ----------------------------------------------------------------------------
# Layout: this file is a thin orchestrator. Dependency discovery lives under
# cmake/, and every library/executable under src/ builds from its own
# CMakeLists.txt via recursive add_subdirectory. Included (not added) so the
# cache variables the dependency modules set (GLAD_TARGET, GLM_INCLUDE_DIR,
# GLAD_SOURCE, ASSIMP_SOURCE) land in this scope and propagate to the subdirs.
# ----------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(Dependencies)   # GLFW + GLAD + GLM + stb -> GLAD_TARGET / GLM_INCLUDE_DIR
include(Assimp)         # optional vendored assimp subproject -> assimp::assimp

add_subdirectory(src)   # gldx, gldxwin, gldxcli, GLFW_Template, demo/*

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
