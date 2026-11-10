# ============================================================================
# FreeType (font rasteriser) — vendored as a git submodule and built from
# source as a static subproject, so the glyph atlas does not depend on which
# system font package happens to be installed on a CI image.
#
# GLDX_ENABLE_FREETYPE=OFF (option declared in the root CMakeLists) drops the
# dependency entirely: the submodule is never checked or configured, and
# Font.cpp compiles its stb_truetype fallback path instead. gldx::Font's
# interface, the atlas layout and TextRenderer's metrics are identical either
# way — the backend only decides who rasterises the outlines.
#
# Included from the root CMakeLists, mirroring cmake/Assimp.cmake.
# ============================================================================
if(GLDX_ENABLE_FREETYPE)
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/freetype/CMakeLists.txt)
    message(FATAL_ERROR
        "FreeType submodule not found. Initialize it first:\n"
        "  git submodule update --init --recursive")
endif()

# Configure FreeType before entering its subtree. Static, no install rules, and
# every optional external codec turned OFF: we only rasterise the outlines of
# .ttf/.otf/.ttc faces, which is FreeType built-in territory. Leaving zlib /
# bzip2 / libpng / harfbuzz / brotli discoverable would make the compiled-in
# feature set depend on whichever of them the host happens to have (Homebrew on
# macOS, vcpkg on Windows, nothing on a bare Linux runner) — and harfbuzz in
# particular would add shaping that gldx::Font deliberately does not use, since
# the atlas bakes one glyph per codepoint with its own advance, exactly like the
# stb fallback. FT_ENABLE_ERROR_STRINGS stays off: the engine reports its own
# messages through std::expected.
set(BUILD_SHARED_LIBS OFF                CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB TRUE                 CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 TRUE                CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG TRUE                  CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ TRUE             CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI TRUE               CACHE BOOL "" FORCE)
set(FT_DISABLE_HVF TRUE                  CACHE BOOL "" FORCE)
set(FT_ENABLE_ERROR_STRINGS OFF          CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON                  CACHE BOOL "" FORCE)

# Upstream defines the real target as plain `freetype`; `freetype-interface` is
# its INTERFACE wrapper carrying the `Freetype::Freetype` EXPORT_NAME that
# FindFreetype.cmake would normally provide. Aliasing that name lets src/gldx
# link the same spelling it would use against a system package, and the usage
# requirements (PUBLIC include dirs) reach us through it either way.
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/freetype EXCLUDE_FROM_ALL)
if(TARGET freetype-interface AND NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype-interface)
endif()

# FreeType's own CMakeLists already carries the PUBLIC include dirs and hides
# symbols for a static build, so nothing has to be patched up after it — unlike
# the assimp block, this subtree needs no force-include or fdopen workaround:
# it is pure C with no C++ standard-library exposure.
message(STATUS "FreeType: submodule (static, no external codecs)")
set(FREETYPE_SOURCE "submodule (static, FreeType rasteriser)")
else()
    message(STATUS "FreeType: DISABLED (GLDX_ENABLE_FREETYPE=OFF) - stb_truetype font backend")
    set(FREETYPE_SOURCE "DISABLED (stb_truetype fallback)")
endif()
