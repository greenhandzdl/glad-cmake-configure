cmake_minimum_required(VERSION 3.28)

# ----------------------------------------------------------------------------
# Self-heal the clang helper-tool paths. This must run BEFORE project(): the
# compiler-detection step freezes clang-scan-deps / llvm-ar / llvm-ranlib into
# CMakeFiles/rules.ninja, and rules.ninja is written from whatever the cache
# says at that moment -- a guard placed after project() corrects the cache too
# late and the stale command survives in the generated rules.
#   CMake resolves these helpers through the compiler's *realpath*, i.e.
#   Cellar/llvm/<version>. `brew upgrade llvm` removes the old Cellar directory
#   but leaves the cache pointing into it, and nothing re-resolves them: a plain
#   `cmake <dir>` keeps the dead value, `-U` empties the command instead (which
#   then fails as "/bin/sh: : command not found"), and a one-off `-D` is not
#   guaranteed to rewrite rules.ninja. The failure looks exactly like "the
#   submodules stopped compiling": every C++20 named-module dependency-scan step
#   exits 127 / command-not-found, so gldx never builds and every demo linking
#   it reports errors.
#   Re-point each helper at the directory of the compiler actually in use --
#   the opt/llvm symlink, which survives the next upgrade. A no-op when the
#   helper is already valid, already absent (MSVC / Apple toolchain) or when
#   this is the first configure (no compiler in the cache yet).
# ----------------------------------------------------------------------------
function(gldx_heal_compiler_helper helper_var helper_dir helper_name)
    set(current_value "${${helper_var}}")
    if(current_value AND EXISTS "${current_value}")
        return()
    endif()
    set(candidate_value "${helper_dir}/${helper_name}")
    if(NOT EXISTS "${candidate_value}")
        return()
    endif()
    set(${helper_var} "${candidate_value}" CACHE FILEPATH
        "Helper tool for ${helper_name}" FORCE)
    if(current_value)
        message(STATUS "${helper_var}: '${current_value}' is gone "
                "(toolchain upgraded?), re-pointed to ${candidate_value}")
    endif()
endfunction()

if(CMAKE_CXX_COMPILER OR CMAKE_C_COMPILER)
    if(CMAKE_CXX_COMPILER)
        get_filename_component(GLDX_CXX_TOOL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    else()
        get_filename_component(GLDX_CXX_TOOL_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    endif()
    if(CMAKE_C_COMPILER)
        get_filename_component(GLDX_C_TOOL_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    else()
        set(GLDX_C_TOOL_DIR "${GLDX_CXX_TOOL_DIR}")
    endif()
    # Written out explicitly (no loop) because these cache variable names do
    # not derive from the tool name, and any string-parsing scheme would choke
    # on Windows paths ("C:/...") that already contain a separator character.
    gldx_heal_compiler_helper(CMAKE_C_COMPILER_CLANG_SCAN_DEPS "${GLDX_C_TOOL_DIR}" clang-scan-deps)
    gldx_heal_compiler_helper(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${GLDX_CXX_TOOL_DIR}" clang-scan-deps)
    gldx_heal_compiler_helper(CMAKE_C_COMPILER_AR "${GLDX_C_TOOL_DIR}" llvm-ar)
    gldx_heal_compiler_helper(CMAKE_CXX_COMPILER_AR "${GLDX_CXX_TOOL_DIR}" llvm-ar)
    gldx_heal_compiler_helper(CMAKE_C_COMPILER_RANLIB "${GLDX_C_TOOL_DIR}" llvm-ranlib)
    gldx_heal_compiler_helper(CMAKE_CXX_COMPILER_RANLIB "${GLDX_CXX_TOOL_DIR}" llvm-ranlib)
endif()

project(GLFW_Template VERSION 1.5.0 LANGUAGES C CXX)

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
