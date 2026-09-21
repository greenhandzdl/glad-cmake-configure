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
    target_link_libraries(d_${name} PRIVATE gldx gldxwin gldxcli)
    # Put the demo's own directory on the include path so split-out headers are
    # included by a stable path ("Scene.h" or "terrain/Chunk.h") regardless of
    # which .cpp in the demo pulls them in.
    target_include_directories(d_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${name})
    set_target_properties(d_${name} PROPERTIES
        OUTPUT_NAME ${name}
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)
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
