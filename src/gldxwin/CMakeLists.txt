# ============================================================================
# gldxwin — engine-agnostic GLFW windowing + frame loop, as a C++20 named module
# ============================================================================
# Replaces the lifecycle half of the old src/demo/demo_app.h: an App singleton
# (glfwInit/Terminate + GL 4.1 core hints) driving any number of Window objects,
# each with OnCreate/OnFrame/OnDestroy hooks and a user-data slot. It links only
# glfw + GLAD and imports no gldx type, so a demo owns its gldx::Renderer through
# the Window rather than the library reaching into the engine. glad/glfw attach to
# the global module via gmf.hpp; importers textually include gldx/core/Platform.h
# to call glfw* themselves.
add_library(gldxwin STATIC App.cpp Window.cpp)

target_sources(gldxwin PUBLIC FILE_SET CXX_MODULES FILES gldxwin.cppm)

target_include_directories(gldxwin PUBLIC ${CMAKE_SOURCE_DIR}/src)

# GLAD_TARGET is set in the root scope by cmake/Dependencies.cmake.
target_link_libraries(gldxwin PUBLIC
    glfw
    ${GLAD_TARGET})

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(gldxwin PRIVATE /utf-8)
endif()
