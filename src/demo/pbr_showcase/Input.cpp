#include "Input.h"

#include <algorithm>

namespace pbr_showcase {

const char* const kFontCandidates[] = {
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Helvetica.ttc",
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "C:/Windows/Fonts/consola.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
};
const int kFontCandidateCount = static_cast<int>(sizeof(kFontCandidates) / sizeof(kFontCandidates[0]));

glm::vec3 SunToward(const Input& in) {
    const float ce = std::cos(in.sunElevation);
    return glm::normalize(glm::vec3(ce * std::sin(in.sunAzimuth),
                                    std::sin(in.sunElevation),
                                    ce * std::cos(in.sunAzimuth)));
}

void MouseCallback(GLFWwindow* win, double x, double y) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in || !in->dragging) return;
    const float dx = static_cast<float>(x - in->lastX);
    const float dy = static_cast<float>(y - in->lastY);
    in->yaw   -= dx * 0.006f;
    in->pitch += dy * 0.006f;
    in->pitch = std::clamp(in->pitch, -1.45f, 1.45f);
    in->lastX = x;
    in->lastY = y;
}

void MouseButtonCallback(GLFWwindow* win, int button, int action, int) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    double cx = 0, cy = 0;
    glfwGetCursorPos(win, &cx, &cy);
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {           // request a pick at this pixel
            int ww = 0, wh = 0;
            glfwGetWindowSize(win, &ww, &wh);
            if (ww > 0 && wh > 0) {
                in->pickPending = true;
                in->pickX = static_cast<float>(cx / ww);   // window-normalised
                in->pickY = static_cast<float>(cy / wh);
            }
        }
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    in->dragging = (action == GLFW_PRESS);
    in->lastX = cx;
    in->lastY = cy;
}

void ScrollCallback(GLFWwindow* win, double, double dy) {
    auto* in = static_cast<Input*>(glfwGetWindowUserPointer(win));
    if (!in) return;
    in->radius = std::clamp(in->radius - static_cast<float>(dy) * 0.6f, 2.0f, 60.0f);
}

void HandleKeys(GLFWwindow* win, Input& in) {
    const float step = 0.04f;
    if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS)
        in.sunAzimuth -= step;
    if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS)
        in.sunAzimuth += step;
    if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS)
        in.sunElevation = std::min(in.sunElevation + step, 1.5f);
    if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS)
        in.sunElevation = std::max(in.sunElevation - step, 0.05f);
}

} // namespace pbr_showcase
