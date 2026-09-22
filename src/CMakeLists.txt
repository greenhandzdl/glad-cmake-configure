# ============================================================================
# src — the three libraries and every executable, each from its own CMakeLists
# ============================================================================
# Recursive, per-module layout: the engine, the window library and the CLI
# library are built first, then the two executable groups consume them. Order
# matters only for readability (target_link_libraries resolves by name); CMake
# sequences the actual build via the module import graph + link dependencies.
add_subdirectory(gldx)
add_subdirectory(gldxwin)
add_subdirectory(gldxcli)

# The applications only call GLFW (shared) and GLAD (static, loads GL at
# runtime), so no direct OpenGL / X11 / Cocoa linking is required here — those
# dependencies are carried transitively by the GLFW and GLAD targets, which the
# gldx library links PUBLIC. Each executable is a sole consumer of the imports.
#
#   GLFW_Template  the minimum-pipeline hello-triangle (main.cpp), drawn through
#                  the high-level gldx::Mesh: imports the three libraries and
#                  asks for no optional subsystem. The hand-assembled
#                  VertexArray/GLBuffer variant lives in demo/hello_triangle.
#   demo/*         the feature demos, one executable each, collected by the
#                  nested add_subdirectory (see demo/CMakeLists.txt).
#
# The primary target keeps its historic name so the CI artifact
# GLFW_Template-<platform> is unaffected. It is defined here rather than under
# demo/ so the "what does the engine need to draw anything at all" answer stays
# with the sources. TARGET_NAME is set in the root CMakeLists.
add_executable(${TARGET_NAME} main.cpp)

target_link_libraries(${TARGET_NAME} PRIVATE gldxwin gldxcli gldx)

set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME ${TARGET_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

add_subdirectory(demo)
