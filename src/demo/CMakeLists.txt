# ============================================================================
# src/demo - one executable per feature demo
#
# Every subdirectory here that owns a main.cpp becomes a graphics demo target
# with no further CMake edits: create src/demo/{feature}/main.cpp, reconfigure,
# and it is built into output/{feature}. Each demo imports the three libraries -
# `import gldx;` (engine), `import gldxwin;` (window + frame loop) and `import
# gldxcli;` (CLI flags) - and textually includes "gldx/core/Platform.h" for the
# glad/glfw declarations it calls directly; the src/ include dir and the glad
# include dir are inherited PUBLIC from those library targets.
#
# Target names are prefixed `d_` so they never collide with the primary
# GLFW_Template target or any third-party name, while OUTPUT_NAME drops the
# prefix so the on-disk binary is just `output/{feature}`.
# ============================================================================

function(add_gldx_demo name)
    # A demo is a small standalone C++ project: main.cpp is the thin entry point
    # that only wires the libraries together and proves the program runs, while
    # any heavier content (helper classes, scene builders, per-feature code) may
    # live in sibling .cpp files or in {feature}/ subdirectories. Collect them all
    # recursively so a demo that stays a single main.cpp is just as valid as one
    # that has grown a folder tree.
    file(GLOB_RECURSE demo_sources CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/${name}/*.cpp)
    add_executable(d_${name} ${demo_sources})
    target_link_libraries(d_${name} PRIVATE gldxwin gldxcli gldx)
    # Put the demo's own directory on the include path so split-out headers are
    # included by a stable path ("Scene.h" or "terrain/Chunk.h") regardless of
    # which .cpp in the demo pulls them in.
    target_include_directories(d_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${name})
    set_target_properties(d_${name} PROPERTIES
        OUTPUT_NAME ${name}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)
    # MSVC only: /bigobj, as cheap insurance against C1128 - a module importer
    # TU can exceed the default 65,279 addressable COFF sections, and the option
    # costs nothing but section-table size (linker from VC2005 or later).
    #
    # Do not confuse that with C1605, which this option does NOT fix. C1605 was a
    # different bug, now fixed: a TU that imports gldx must not copy an imported
    # `static constexpr` class-type constant into a namespace-scope `constexpr`
    # variable (`constexpr glm::vec4 kDim = Hud::kDim;`) and then ODR-use that
    # alias. MSVC 14.44's object writer never terminates on the definition it then
    # owes the importer; clang emits it without complaint, so nothing local can
    # see the difference. Run 42781895430's matrix B is the controlled experiment:
    # on the minimal failing prefix, deleting the one call (alias declarations
    # still present) compiles, naming `Hud::kDim` directly compiles into ~6.7 KB,
    # and every rewrite that still ODR-uses the alias (prvalue of it, a local
    # copy, a local const reference) gives C1605. So the trigger is the ODR-use,
    # not the declaration. The fix removed the aliases from glass_water/Hud.cpp
    # and night_forest/Hud.cpp and spells `Hud::kX` at every call site; the
    # forensic rounds that got there are in CHANGELOG.md and in the header of
    # scripts/msvc-object-probe.sh, which still runs on any Windows failure.
    #
    # C1605 was not the last thing hiding behind it. With the aliases gone the
    # next Windows run got one step further - to voxel_terrain/Hud.cpp, which no
    # earlier run had reached - and died there on a redefinition storm instead
    # (C2011 'glm::qualifier', C2572/C2953, then the same for <vector>). That is
    # the *importer environment* problem, not an object-writer one: the module
    # was built with /utf-8, every importer without it, so MSVC flagged each
    # import with C5050 (114 of them in that build) and then re-parsed the
    # global-module headers the module had already attached. /utf-8 is now a
    # PUBLIC requirement of gldx/gldxwin/gldxcli so importers match by
    # construction, and importers include their global-module headers (glm, std)
    # BEFORE the import declarations - see invariant 7 in AGENTS.md. Both are
    # clang-invisible, hence this note.
    if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(d_${name} PRIVATE /bigobj)
    endif()
endfunction()

# CONFIGURE_DEPENDS makes the glob re-run at build time, so dropping in a new
# src/demo/{feature}/main.cpp gets picked up without a manual reconfigure.
file(GLOB demo_children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)

foreach(child ${demo_children})
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${child}/main.cpp)
        add_gldx_demo(${child})
    endif()
endforeach()
