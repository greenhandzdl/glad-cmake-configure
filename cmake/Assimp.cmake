# ============================================================================
# Assimp (3D model import library) — vendored as a git submodule and built
# from source as a static subproject. This keeps the build self-contained
# across all three CI platforms (no system/vcpkg assimp package needed).
#
# GLDX_ENABLE_ASSIMP=OFF (the option is declared in the root CMakeLists) drops
# the entire model-import path (and this heavy dependency) out of the build:
# the submodule is never checked or configured, gldx links without assimp, and
# ModelLoader::Load reports unavailability at runtime. The exported `gldx` module
# interface is unchanged either way.
#
# Included from the root CMakeLists, so the add_subdirectory lands the assimp
# tree at the same build/ location it used when the block sat inline there.
# ============================================================================
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
