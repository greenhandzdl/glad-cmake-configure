#ifndef GLDXWIN_GMF_HPP
#define GLDXWIN_GMF_HPP

/**
 * @file gmf.hpp
 * @brief Shared GLOBAL MODULE FRAGMENT includes for the gldxwin named module.
 *
 * Same convention the engine module gldx follows: every translation unit of
 * gldxwin (the primary interface gldxwin.cppm and the `module gldxwin;`
 * implementation units App.cpp / Window.cpp) textually includes THIS header
 * inside its `module;` global module fragment, before the `export module
 * gldxwin;` / `module gldxwin;` directive. That attaches GLAD and GLFW (and a
 * small std set) to the *global module*, so the exported signatures — GLFWwindow*
 * handles, std::function callbacks — reference global-module entities that the
 * importing demo reaches through its own text include of gldx/core/Platform.h.
 *
 * The include ORDER matters: <glad/gl.h> must precede <GLFW/glfw3.h> or GLFW
 * pulls the system <GL/gl.h> and clashes with our loader (the same hazard the
 * engine's Platform.h documents). This library is engine-agnostic: it knows
 * GLFW/GLAD only, never a gldx type.
 */

// Loader before windowing — mirrors gldx/core/Platform.h ordering.
#include <glad/gl.h>
#include <GLFW/glfw3.h>

// The single engine hook gldxwin may call (PNG encoding for
// Window::CaptureScreenshot). Included here, in the global module fragment, so
// it attaches to the global module on both sides of the link - see the header's
// comment. This stays a declaration-only include; gldxwin still links just
// glfw + GLAD and imports no gldx module.
#include "gldx/util/ScreenshotHook.h"

// Standard-library headers the public API is expressed with (cstdint/cstring
// back the raw readback buffer inside Window::CaptureScreenshot).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#endif // GLDXWIN_GMF_HPP
