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

void ApplyCapture(gldx::win::Window& win, Input& in, bool keepCursor) {
    in.captured = !keepCursor && (in.cam == Input::Cam::Fly);
    win.SetCursorCaptured(in.captured);
}

void MouseCallback(gldx::win::Window& win, gldx::win::Vec2d pos) {
    auto* in = static_cast<Input*>(win.GetUserData());
    if (!in) return;
    const float dx = static_cast<float>(pos.x - in->lastX);
    const float dy = static_cast<float>(pos.y - in->lastY);
    in->lastX = pos.x;
    in->lastY = pos.y;
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

void MouseButtonCallback(gldx::win::Window& win, gldx::win::MouseButton button,
                         gldx::win::KeyAction action, int) {
    auto* in = static_cast<Input*>(win.GetUserData());
    if (!in || action != gldx::win::KeyAction::Press) return;
    if (button == gldx::win::MouseButton::Left) {
        if (in->cam == Input::Cam::Fly) in->wantBreak = true;
        else { in->dragging = true; const gldx::win::Vec2d c = win.CursorPos();
               in->lastX = c.x; in->lastY = c.y; }
    } else if (button == gldx::win::MouseButton::Right) {
        if (in->cam == Input::Cam::Fly) in->wantPlace = true;
    }
}

void ScrollCallback(gldx::win::Window& win, double, double dy) {
    auto* in = static_cast<Input*>(win.GetUserData());
    if (!in) return;
    in->orbitRadius = std::clamp(in->orbitRadius - static_cast<float>(dy) * 1.2f, 6.0f, 120.0f);
}

} // namespace voxel_terrain
