#ifndef GLDX_CORE_PLATFORM_H
#define GLDX_CORE_PLATFORM_H

/**
 * @file Platform.h
 * @brief Platform detection + the GLAD-before-GLFW include ordering.
 *
 * This was formerly the template's global `src/headers/common.h`. It is the
 * single place that guarantees <glad/gl.h> is seen before <GLFW/glfw3.h> (GLFW
 * otherwise pulls the system <GL/gl.h> and clashes with our loader). App/window
 * identity constants now live with the individual demos, so this header carries
 * nothing beyond the platform macros and the include ordering.
 */

// Platform detection (kept as macros so callers can use them in #if).
#if defined(__APPLE__)
    #define GLFW_PLATFORM_MACOS 1
    #define GLFW_PLATFORM_LINUX 0
    #ifndef GL_SILENCE_DEPRECATION
        #define GL_SILENCE_DEPRECATION
    #endif
#elif defined(__linux__)
    #define GLFW_PLATFORM_MACOS 0
    #define GLFW_PLATFORM_LINUX 1
#else
    #define GLFW_PLATFORM_MACOS 0
    #define GLFW_PLATFORM_LINUX 0
#endif

// GLAD must be included before GLFW to avoid gl.h conflicts.
#include <glad/gl.h>
#include <GLFW/glfw3.h>

#endif // GLDX_CORE_PLATFORM_H
