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
# GLAD (OpenGL Loader)
#   Priority 1: system package (gladConfig.cmake, e.g. brew install glad)
#   Priority 2: build from git submodule (https://github.com/Dav1dde/glad.git)
# ============================================================================
set(GLAD_FOUND FALSE)

find_package(glad CONFIG QUIET)
if(TARGET glad::glad)
    message(STATUS "Found GLAD (system package: glad::glad)")
    set(GLAD_TARGET glad::glad)
    set(GLAD_SOURCE "system")
    set(GLAD_FOUND TRUE)
endif()

if(NOT GLAD_FOUND)
    # Fallback: build GLAD from the Dav1dde/glad git submodule.
    # The submodule is the glad2 generator (Python package). We invoke it
    # directly via PYTHONPATH=third_party/glad instead of using its bundled
    # cmake/GladConfig.cmake: glad_add_library() passes the generator as a
    # bare positional argument (e.g. "... --api gl:core=3.3 c") which is only
    # compatible with the exact in-tree CLI, and does not set up the Python
    # path so the generator can be found.
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/glad/glad/__main__.py)
        message(FATAL_ERROR "GLAD submodule not found. Please initialize it:\n"
            "  git submodule update --init --recursive")
    endif()

    find_package(Python COMPONENTS Interpreter REQUIRED)

    # The glad2 generator requires jinja2. Prefer the system Python if it
    # already has jinja2; otherwise create an isolated uv venv with jinja2
    # (keeps the global environment untouched and avoids PEP 668 issues).
    set(GLAD_GEN_PYTHON ${Python_EXECUTABLE})
    execute_process(
        COMMAND ${Python_EXECUTABLE} -c "import jinja2"
        RESULT_VARIABLE GLAD_JINJA2_RC
        OUTPUT_QUIET ERROR_QUIET
    )
    if(GLAD_JINJA2_RC EQUAL 0)
        message(STATUS "GLAD generator: system Python (jinja2 available)")
    else()
        find_program(UV_EXECUTABLE uv)
        if(NOT UV_EXECUTABLE)
            message(FATAL_ERROR "System Python has no jinja2 and 'uv' was not found.\n"
                "  Install uv (https://docs.astral.sh/uv/) or jinja2:\n"
                "  macOS: brew install uv   or   ${Python_EXECUTABLE} -m pip install jinja2")
        endif()
        set(GLAD_VENV_DIR ${CMAKE_CURRENT_BINARY_DIR}/glad_venv)
        if(WIN32)
            set(GLAD_VENV_PYTHON ${GLAD_VENV_DIR}/Scripts/python.exe)
        else()
            set(GLAD_VENV_PYTHON ${GLAD_VENV_DIR}/bin/python)
        endif()
        if(NOT EXISTS ${GLAD_VENV_PYTHON})
            message(STATUS "Creating uv venv for GLAD generator (${GLAD_VENV_DIR})...")
            execute_process(
                COMMAND ${UV_EXECUTABLE} venv ${GLAD_VENV_DIR}
                RESULT_VARIABLE GLAD_VENV_RC
            )
            if(NOT GLAD_VENV_RC EQUAL 0)
                message(FATAL_ERROR "Failed to create uv venv for GLAD generator:\n"
                    "  ${UV_EXECUTABLE} venv ${GLAD_VENV_DIR}")
            endif()
            execute_process(
                COMMAND ${UV_EXECUTABLE} pip install --python ${GLAD_VENV_PYTHON} jinja2
                RESULT_VARIABLE GLAD_PIP_RC
            )
            if(NOT GLAD_PIP_RC EQUAL 0)
                message(FATAL_ERROR "Failed to install jinja2 into GLAD venv:\n"
                    "  ${UV_EXECUTABLE} pip install --python ${GLAD_VENV_PYTHON} jinja2")
            endif()
        endif()
        set(GLAD_GEN_PYTHON ${GLAD_VENV_PYTHON})
        message(STATUS "GLAD generator: uv venv (${GLAD_VENV_DIR})")
    endif()

    set(GLAD_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gladsources/glad_gl_core_33)
    set(GLAD_GEN_GL   ${GLAD_GEN_DIR}/src/gl.c)
    set(GLAD_GEN_GL_H ${GLAD_GEN_DIR}/include/glad/gl.h)
    set(GLAD_GEN_KHR  ${GLAD_GEN_DIR}/include/KHR/khrplatform.h)

    message(STATUS "System GLAD not found; building from submodule (third_party/glad)")
    add_custom_command(
        OUTPUT ${GLAD_GEN_GL} ${GLAD_GEN_GL_H} ${GLAD_GEN_KHR}
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${GLAD_GEN_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GLAD_GEN_DIR}
        COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${CMAKE_SOURCE_DIR}/third_party/glad"
            ${GLAD_GEN_PYTHON} -m glad
            --out-path ${GLAD_GEN_DIR} --api gl:core=3.3 --reproducible c
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${CMAKE_SOURCE_DIR}/third_party/glad/glad/__main__.py
        COMMENT "Generating GLAD from submodule (glad2, OpenGL 3.3 Core)"
        VERBATIM
    )

    add_library(glad_gl_core_33 STATIC ${GLAD_GEN_GL})
    target_include_directories(glad_gl_core_33 PUBLIC ${GLAD_GEN_DIR}/include)
    target_link_libraries(glad_gl_core_33 PUBLIC ${CMAKE_DL_LIBS})

    set(GLAD_TARGET glad_gl_core_33)
    set(GLAD_SOURCE "submodule (OpenGL 3.3 Core)")
    set(GLAD_FOUND TRUE)
endif()

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
# STB (header-only image/utility library - git submodule)
#
# stb is a header-only library. The implementation macros
# (e.g. STB_IMAGE_IMPLEMENTATION, STB_IMAGE_WRITE_IMPLEMENTATION) must be
# defined in EXACTLY ONE .cpp file that includes the corresponding header,
# so they are intentionally NOT defined here (global definitions would cause
# duplicate-symbol errors when multiple translation units include stb).
# ============================================================================
if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h)
    message(FATAL_ERROR "STB not found. Please initialize the submodule:\n"
        "  git submodule update --init --recursive")
endif()
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

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
    ${GLAD_TARGET}
    stb
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
message(STATUS "  GLAD:            ${GLAD_SOURCE}")
message(STATUS "  GLAD Target:     ${GLAD_TARGET}")
message(STATUS "  GLM:             ${GLM_INCLUDE_DIR}")
message(STATUS "  STB:             submodule (${CMAKE_SOURCE_DIR}/third_party/stb)")
message(STATUS "  Output Directory: ${CMAKE_SOURCE_DIR}/output")
message(STATUS "")
