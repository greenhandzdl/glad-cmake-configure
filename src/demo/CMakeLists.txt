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
    # Do not confuse that with C1605, which this option does NOT fix. The v1.7.0
    # Windows job failed with `fatal error C1605: compiler limit: object file
    # size cannot exceed 4 GB` on `glass_water/Hud.cpp` and `night_forest/Hud.cpp`;
    # reading the 4 GB as "/bigobj's 2^32 section capacity" looked convincing and
    # was wrong - run 42760865230 shows /bigobj on the cl command line with the
    # identical error. "The TU is too big" is wrong too: night_forest/main.cpp
    # and glass_water/main.cpp are larger, import gldx and include the same Hud.h,
    # and compile fine. "Importing several modules is what does it" is wrong too:
    # counting the C5050 lines by the source path each one carries, 18 demo sources
    # import gldxwin (many of them gldx + gldxwin + gldxcli) while the whole build
    # fails on exactly two objects. So the trigger is inside the bodies of those
    # two files, and scripts/msvc-object-probe.sh now names the statement. Run
    # 42765315428: all six flag variants give the byte-identical error, a skeleton
    # TU with the same `import gldx;` + Hud.h preamble and an empty body compiles
    # into 1.5 KB, and prefixes of glass_water/Hud.cpp pass up to L85 and fail from
    # L97 onward - with every failing prefix taking 11.1 s and every passing one
    # 1.2 s. Time that does not grow with the amount of code is not a size limit.
    # Run 42770837956 then bisected both TUs down to the *same* statement,
    # `hud.AddLine(buf, kDim);` (glass_water L86 after L85 passes, night_forest L57
    # after L56), and closed the last two axes: /Od, /Gw and /GS- change nothing,
    # and the /Fa listing reaches only 285 bytes and 12 lines before the watchdog
    # fires - it stops in the prologue, before a single instruction is emitted, so
    # the writer is not being handed garbage; it runs away on its own.
    # Run 42777226104 then falsified that headline with its own control row: deleting
    # the statement from the complete TU still gives C1605, on both files. The two
    # results only stand together as sufficient-but-not-necessary - inside the prefix
    # family that one line crosses the limit, while the complete file has the same
    # call shape 7 times (glass_water) and 12 times (night_forest), so removing one
    # site from something already over the limit changes nothing. Whether all the
    # sites have to go, or the file is simply over a cumulative limit and AddLine is
    # only where the prefix happened to cross it, is what the new
    # delete-all-occurrences row is there to answer. Per-property rewrites mean
    # nothing on the complete file for that reason, and run on the minimal failing
    # prefix instead - there, deleting the statement really does compile again, and if
    # that stops holding the probe prints CONTROL DID NOT COMPILE rather than letting
    # the rows below it be read as evidence.
    # Which property of the call is unloadable is still open. The candidates come
    # from the one asymmetry left in the source: both failing TUs declare
    # `constexpr glm::vec4 kDim = Hud::kDim;` and hand over the *local* name, while
    # text_hud/main.cpp passes `Hud::kDim` directly and voxel_terrain/Hud.cpp
    # declares no alias at all. That is an observation about which files fail, not a
    # diagnosis, and even a row that compiles only names a form the writer can emit -
    # a fix would have to be applied at every call site, not at the one the bisect
    # happened to name. Until then this stays "known, not fixed", and remember that
    # clang has no equivalent limit, so nothing built here can observe any of it.
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
