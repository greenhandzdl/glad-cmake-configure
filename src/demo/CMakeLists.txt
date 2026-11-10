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
    # the writer is not being handed garbage; it runs away on its own. What is still
    # open is which *property* of that call MSVC cannot write. The probe now
    # rewrites just that region of the complete TU, once per property: the imported
    # constant instead of this file's `constexpr glm::vec4 kDim = Hud::kDim;`, a
    # prvalue of it, a function-local copy, a function-local const ref, and a
    # literal in place of the char array that has to convert to std::string_view -
    # motivated by the importers that compile either passing `Hud::kDim` directly or
    # declaring no alias at all, which is an observation about which files fail,
    # not yet a diagnosis. Until one of those rewrites compiles this stays
    # "known, not fixed" - and remember that clang has no equivalent limit, so
    # nothing built here can observe any of it.
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
