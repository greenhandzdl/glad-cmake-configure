#ifndef PBR_SHOWCASE_INPUT_H
#define PBR_SHOWCASE_INPUT_H

/**
 * @file Input.h
 * @brief Orbit-camera + interaction state for the pbr_showcase demo, and the
 *        GLFW callbacks / per-frame key handling that mutate it.
 *
 * Pulled out of main.cpp so the demo reads like a small project: main wires the
 * engine together, this file owns "how the user drives the camera and toggles".
 */

#include "gldx/core/Platform.h"   // <glad/gl.h> + <GLFW/glfw3.h>

#include <glm/glm.hpp>

namespace pbr_showcase {

struct Input {
    float yaw   = 0.65f;    // azimuth around the target (rad)
    float pitch = 0.42f;    // elevation above the target (rad)
    float radius = 11.0f;   // distance to the target
    double lastX = 0.0;
    double lastY = 0.0;
    bool  dragging = false;
    float sunAzimuth = 0.7f;
    float sunElevation = 0.85f;   // radians above the horizon
    bool  useShadow = true;
    bool  useIbl = true;
    bool  useBloom = true;
    bool  useDebug = false;
    bool  useInstances = false;
    bool  useSky = true;     // background cube; off leaves the clear colour
    bool  ortho = false;   // current projection: false=perspective, true=ortho
    // Pending right-click pick request (consumed + cleared in the main loop).
    // Stored normalised to the window, not raw cursor pixels: the pick ray is
    // built in framebuffer pixels, which differ by the content scale (2 on
    // Retina) from glfwGetCursorPos' window coordinates.
    bool  pickPending = false;
    float pickX = 0.0f, pickY = 0.0f;   // [0..1] across the window
};

// Unit vector pointing from the scene toward the sun (elevation > 0 => upward).
glm::vec3 SunToward(const Input& in);

void MouseCallback(GLFWwindow* win, double x, double y);
void MouseButtonCallback(GLFWwindow* win, int button, int action, int);
void ScrollCallback(GLFWwindow* win, double, double dy);
void HandleKeys(GLFWwindow* win, Input& in);

// A few well-known system fonts, tried in order; HUD text just no-ops if none
// are found, so a missing font never breaks the render.
extern const char* const kFontCandidates[];
extern const int kFontCandidateCount;

} // namespace pbr_showcase

#endif // PBR_SHOWCASE_INPUT_H
