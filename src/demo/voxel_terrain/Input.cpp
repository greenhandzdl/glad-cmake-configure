// voxel_terrain demo — camera / interaction implementation (see Input.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Input.h"

#include <algorithm>

#include <glm/glm.hpp>

namespace voxel_terrain {

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};
const int kFontCandidateCount =
    static_cast<int>(sizeof(kFontCandidates) / sizeof(kFontCandidates[0]));

glm::vec3 SunToward(const Input& in) {
    const float ce = std::cos(in.sunElevation);
    return glm::normalize(glm::vec3(ce * std::sin(in.sunAzimuth),
                                    std::sin(in.sunElevation),
                                    ce * std::cos(in.sunAzimuth)));
}

void ApplyCapture(GLFWwindow* win, Input& in, bool keepCursor) {
    in.captured = !keepCursor && (in.cam == Input::Cam::Fly);
    glfwSetInputMode(win, GLFW_CURSOR,
                     in.captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void MouseCallback(GLFWwindow* win, double x, double y) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    const float dx = static_cast<float>(x - in->lastX);
    const float dy = static_cast<float>(y - in->lastY);
    in->lastX = x;
    in->lastY = y;
    if (in->captured) {
        // FPS look: right on the mouse moves the view right (positive yaw turns
        // left in the engine's convention), down moves it down.
        in->yaw   -= dx * 0.0032f;
        in->pitch -= dy * 0.0032f;
        in->pitch = std::clamp(in->pitch, -1.5f, 1.5f);
        return;
    }
    if (!in->dragging) return;
    in->orbitYaw   -= dx * 0.006f;
    in->orbitPitch = std::clamp(in->orbitPitch + dy * 0.006f, -1.45f, 1.45f);
}

void MouseButtonCallback(GLFWwindow* win, int button, int action, int) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in || action != GLFW_PRESS) return;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (in->cam == Input::Cam::Fly) in->wantBreak = true;
        else { in->dragging = true; double cx = 0, cy = 0; glfwGetCursorPos(win, &cx, &cy);
               in->lastX = cx; in->lastY = cy; }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (in->cam == Input::Cam::Fly) in->wantPlace = true;
    }
}

void ScrollCallback(GLFWwindow* win, double, double dy) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    in->orbitRadius = std::clamp(in->orbitRadius - static_cast<float>(dy) * 1.2f, 6.0f, 120.0f);
}

} // namespace voxel_terrain
