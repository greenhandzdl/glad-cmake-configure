# ============================================================================
# Third-party dependencies: GLFW, GLAD, GLM, STB.
#
# Included from the top-level CMakeLists.txt (so it runs in the root scope and
# the GLAD_TARGET / GLAD_SOURCE / GLM_INCLUDE_DIR it sets are visible to every
# add_subdirectory below it). Kept out of the root file so the orchestrator
# stays short; the behaviour is byte-for-byte what used to sit inline there.
# ============================================================================

# ============================================================================
# GLFW  (Homebrew / apt / vcpkg all export an imported target `glfw`)
# ============================================================================
find_package(glfw3 QUIET)
if(NOT TARGET glfw)
    message(FATAL_ERROR
        "GLFW3 not found. Install it first:\n"
        "  macOS: brew install glfw\n"
        "  Linux: sudo apt-get install libglfw3-dev\n"
        "  Windows: vcpkg install glfw3")
endif()

# ============================================================================
# GLAD  (OpenGL loader)
#   1) prefer a system package (find_package(glad CONFIG) -> glad::glad)
#   2) otherwise generate bindings from the third_party/glad submodule
#      (glad2 generator: Python 3 + jinja2; falls back to an isolated uv venv)
# ============================================================================
find_package(glad CONFIG QUIET)
if(TARGET glad::glad)
    message(STATUS "GLAD: system package (glad::glad)")
    set(GLAD_TARGET glad::glad)
    set(GLAD_SOURCE "system")
else()
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/glad/glad/__main__.py)
        message(FATAL_ERROR
            "GLAD submodule not found. Initialize it first:\n"
            "  git submodule update --init --recursive")
    endif()

    find_package(Python COMPONENTS Interpreter REQUIRED)

    # The glad2 generator needs jinja2. Use the system Python when it already
    # provides jinja2; otherwise create an isolated uv venv (keeps the global
    # interpreter clean and sidesteps PEP 668).
    set(GLAD_GEN_PYTHON ${Python_EXECUTABLE})
    execute_process(
        COMMAND ${Python_EXECUTABLE} -c "import jinja2"
        RESULT_VARIABLE GLAD_JINJA2_RC OUTPUT_QUIET ERROR_QUIET)
    if(NOT GLAD_JINJA2_RC EQUAL 0)
        find_program(UV_EXECUTABLE uv)
        if(NOT UV_EXECUTABLE)
            message(FATAL_ERROR
                "System Python has no jinja2 and 'uv' was not found.\n"
                "  Install uv (https://docs.astral.sh/uv/) or jinja2 (pip install jinja2)")
        endif()
        set(GLAD_VENV_DIR ${CMAKE_CURRENT_BINARY_DIR}/glad_venv)
        if(WIN32)
            set(GLAD_VENV_PYTHON ${GLAD_VENV_DIR}/Scripts/python.exe)
        else()
            set(GLAD_VENV_PYTHON ${GLAD_VENV_DIR}/bin/python)
        endif()
        if(NOT EXISTS ${GLAD_VENV_PYTHON})
            message(STATUS "GLAD: creating uv venv for the generator (${GLAD_VENV_DIR})")
            execute_process(
                COMMAND ${UV_EXECUTABLE} venv ${GLAD_VENV_DIR}
                RESULT_VARIABLE GLAD_VENV_RC)
            if(NOT GLAD_VENV_RC EQUAL 0)
                message(FATAL_ERROR "Failed to create uv venv: ${UV_EXECUTABLE} venv ${GLAD_VENV_DIR}")
            endif()
            execute_process(
                COMMAND ${UV_EXECUTABLE} pip install --python ${GLAD_VENV_PYTHON} jinja2
                RESULT_VARIABLE GLAD_PIP_RC)
            if(NOT GLAD_PIP_RC EQUAL 0)
                message(FATAL_ERROR "Failed to install jinja2 into the GLAD venv")
            endif()
        endif()
        set(GLAD_GEN_PYTHON ${GLAD_VENV_PYTHON})
    endif()

    set(GLAD_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gladsources/glad_gl_core_41)
    add_custom_command(
        OUTPUT ${GLAD_GEN_DIR}/src/gl.c ${GLAD_GEN_DIR}/include/glad/gl.h ${GLAD_GEN_DIR}/include/KHR/khrplatform.h
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${GLAD_GEN_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GLAD_GEN_DIR}
        COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${CMAKE_SOURCE_DIR}/third_party/glad"
            ${GLAD_GEN_PYTHON} -m glad --out-path ${GLAD_GEN_DIR} --api gl:core=4.1 --reproducible c
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        DEPENDS ${CMAKE_SOURCE_DIR}/third_party/glad/glad/__main__.py
        COMMENT "Generating GLAD from submodule (OpenGL 4.1 Core)"
        VERBATIM)

    add_library(glad_gl_core_41 STATIC ${GLAD_GEN_DIR}/src/gl.c)
    target_include_directories(glad_gl_core_41 PUBLIC ${GLAD_GEN_DIR}/include)
    target_link_libraries(glad_gl_core_41 PUBLIC ${CMAKE_DL_LIBS})

    set(GLAD_TARGET glad_gl_core_41)
    set(GLAD_SOURCE "submodule (OpenGL 4.1 Core)")
endif()

# ============================================================================
# GLM (header-only)  &  STB (header-only submodule)
# ============================================================================
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    HINTS ${BREW_PREFIX}/include /usr/local/include /usr/include
          ${CMAKE_SOURCE_DIR}/third_party)
if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR
        "GLM not found. Install it first:\n"
        "  macOS: brew install glm\n"
        "  Linux: sudo apt-get install libglm-dev\n"
        "  Windows: vcpkg install glm")
endif()

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h)
    message(FATAL_ERROR
        "STB submodule not found. Initialize it first:\n"
        "  git submodule update --init --recursive")
endif()
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_SOURCE_DIR}/third_party/stb)
