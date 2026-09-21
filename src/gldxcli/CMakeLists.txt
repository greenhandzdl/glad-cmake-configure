# ============================================================================
# gldxcli — the demo-side command-line switch parser, as a C++20 named module
# ============================================================================
# Formerly the header-only src/demo/demo_cli.h. It is deliberately independent
# of both the engine (gldx) and the window library (gldxwin): a pure standard-
# library argument parser, so it links nothing and only needs the src/ include
# dir to be reachable by importers. The primary interface unit holds Flags
# fully inline; gldxcli.cpp is a module implementation unit that gives the
# static archive a concrete object alongside the FILE_SET interface.
add_library(gldxcli STATIC gldxcli.cpp)

target_sources(gldxcli PUBLIC FILE_SET CXX_MODULES FILES gldxcli.cppm)

target_include_directories(gldxcli PUBLIC ${CMAKE_SOURCE_DIR}/src)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(gldxcli PRIVATE /utf-8)
endif()
