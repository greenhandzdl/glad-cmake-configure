cmake_minimum_required(VERSION 3.16)
project(GLFW_Template VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Platform Detection
# ============================================================================
if(APPLE)
    message(STATUS "Building on macOS")
    set(IS_MACOS TRUE)
elseif(UNIX)
    message(STATUS "Building on Linux")
    set(IS_LINUX TRUE)
endif()

# ============================================================================
# Set output directory
# ============================================================================
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output)

# ============================================================================
# macOS Specific Configuration
# ============================================================================
if(IS_MACOS)
    # Find Homebrew prefix
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE BREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(BREW_PREFIX)
        message(STATUS "Homebrew prefix: ${BREW_PREFIX}")
        list(APPEND CMAKE_PREFIX_PATH "${BREW_PREFIX}")
        list(APPEND CMAKE_LIBRARY_PATH "${BREW_PREFIX}/lib")
        list(APPEND CMAKE_INCLUDE_PATH "${BREW_PREFIX}/include")
    endif()

    # Cocoa framework for macOS (required for GLFW native backend)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
endif()

# ============================================================================
# Find GLFW
# ============================================================================
# Try to find GLFW3 via CMake config first
find_package(glfw3 QUIET)

if(TARGET glfw)
    message(STATUS "Found GLFW3 (glfw target)")
    set(GLFW_TARGET glfw)
    set(GLFW_FOUND TRUE)
elseif(TARGET glfw::glfw)
    message(STATUS "Found GLFW3 (glfw::glfw target)")
    set(GLFW_TARGET glfw::glfw)
    set(GLFW_FOUND TRUE)
else()
    # Fallback: find_package with CONFIG
    find_package(GLFW3 QUIET CONFIG)
    if(TARGET GLFW3::GLFW)
        message(STATUS "Found GLFW3 (GLFW3::GLFW target)")
        set(GLFW_TARGET GLFW3::GLFW)
        set(GLFW_FOUND TRUE)
    endif()
endif()

if(NOT GLFW_FOUND)
    message(FATAL_ERROR "GLFW3 not found. Please install GLFW:\n"
        "  macOS: brew install glfw\n"
        "  Linux: sudo apt-get install libglfw3-dev")
endif()

# ============================================================================
# GLAD (OpenGL Loader - embedded source)
# ============================================================================
add_library(glad STATIC
    ${CMAKE_SOURCE_DIR}/third_party/glad/src/glad.c
)
target_include_directories(glad PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/glad/include
)

# ============================================================================
# GLM (header-only math library, required by Shader.h)
# ============================================================================
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    HINTS
        ${BREW_PREFIX}/include
        /usr/local/include
        /usr/include
        ${CMAKE_SOURCE_DIR}/third_party
)
if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "GLM not found. Please install GLM:\n"
        "  macOS: brew install glm\n"
        "  Linux: sudo apt-get install libglm-dev")
endif()
message(STATUS "Found GLM: ${GLM_INCLUDE_DIR}")

# ============================================================================
# Find OpenGL
# ============================================================================
if(IS_LINUX)
    find_package(OpenGL REQUIRED)
elseif(IS_MACOS)
    # macOS has OpenGL in system frameworks
    find_library(OPENGL_LIBRARY OpenGL)
endif()

# ============================================================================
# Find required system libraries for Linux
# ============================================================================
if(IS_LINUX)
    find_package(X11 REQUIRED)
    find_package(Threads REQUIRED)
endif()

# ============================================================================
# Source Files
# ============================================================================
set(SOURCE_FILES
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/utils.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/Shader.cpp
)

# ============================================================================
# Executable
# ============================================================================
set(TARGET_NAME "GLFW_Template")

add_executable(${TARGET_NAME}
    ${SOURCE_FILES}
)

target_include_directories(${TARGET_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src/headers
    ${GLM_INCLUDE_DIR}
)

target_link_libraries(${TARGET_NAME} PRIVATE
    ${GLFW_TARGET}
    glad
)

# Platform specific libraries
if(IS_MACOS)
    message(STATUS "Linking macOS frameworks...")
    target_link_libraries(${TARGET_NAME} PRIVATE
        ${OPENGL_LIBRARY}
        ${COCOA_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
    )
    target_compile_definitions(${TARGET_NAME} PRIVATE
        GL_SILENCE_DEPRECATION
    )
elseif(IS_LINUX)
    message(STATUS "Linking Linux libraries...")
    target_link_libraries(${TARGET_NAME} PRIVATE
        ${OPENGL_LIBRARIES}
        X11
        Threads::Threads
        m
        dl
    )
endif()

# Set properties
set_target_properties(${TARGET_NAME} PROPERTIES
    OUTPUT_NAME ${TARGET_NAME}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/output
)

# ============================================================================
# Generate Scripts from Templates
# ============================================================================
set(BUILD_DIR_NAME "build")
set(OUTPUT_DIR_NAME "output")

configure_file(
    ${CMAKE_SOURCE_DIR}/scripts/build.sh.in
    ${CMAKE_SOURCE_DIR}/scripts/build.sh
    @ONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/scripts/clean.sh.in
    ${CMAKE_SOURCE_DIR}/scripts/clean.sh
    @ONLY
)

configure_file(
    ${CMAKE_SOURCE_DIR}/scripts/run.sh.in
    ${CMAKE_SOURCE_DIR}/scripts/run.sh
    @ONLY
)

# Make scripts executable
execute_process(COMMAND chmod +x ${CMAKE_SOURCE_DIR}/scripts/build.sh)
execute_process(COMMAND chmod +x ${CMAKE_SOURCE_DIR}/scripts/clean.sh)
execute_process(COMMAND chmod +x ${CMAKE_SOURCE_DIR}/scripts/run.sh)

# ============================================================================
# Clean Project Target
# ============================================================================
add_custom_target(clean-project
    COMMENT "Cleaning all generated files (build, output, scripts)..."
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/build
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/output
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_SOURCE_DIR}/scripts/build.sh
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_SOURCE_DIR}/scripts/clean.sh
    COMMAND ${CMAKE_COMMAND} -E remove -f ${CMAKE_SOURCE_DIR}/scripts/run.sh
    COMMAND ${CMAKE_COMMAND} -E echo "Clean complete!"
)

# ============================================================================
# Configuration Summary
# ============================================================================
message(STATUS "")
message(STATUS "Configuration Summary:")
message(STATUS "  Build Type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform:        ${CMAKE_SYSTEM_NAME}")
message(STATUS "  GLFW:            ${GLFW_FOUND}")
message(STATUS "  GLFW Target:     ${GLFW_TARGET}")
message(STATUS "  GLAD:            embedded (OpenGL 3.3 Core)")
message(STATUS "  GLM:             ${GLM_INCLUDE_DIR}")
message(STATUS "  Output Directory: ${CMAKE_SOURCE_DIR}/output")
message(STATUS "")
