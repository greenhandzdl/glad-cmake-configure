#ifndef VOXEL_TERRAIN_INPUT_H
#define VOXEL_TERRAIN_INPUT_H

/**
 * @file Input.h
 * @brief Fly/orbit camera + interaction state for the voxel_terrain demo, and
 *        the GLFW callbacks that mutate it.
 *
 * Pulled out of main.cpp so the demo reads like a small project: main wires the
 * engine together and drives the per-frame loop, this file owns "what the user
 * has asked for" (camera pose, selected block, sun, edit requests).
 *
 * NOTE: references the Block alias from World.h, so include World.h first (the
 * .cpp files establish `import gldx;` before including either header).
 */

#include "gldx/core/Platform.h"   // <glad/gl.h> + <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include "World.h"

namespace voxel_terrain {

struct Input {
    enum class Cam { Fly, Orbit };
    Cam cam = Cam::Fly;
    bool captured = true;
    double lastX = 0.0, lastY = 0.0;
    bool dragging = false;
    // Fly orientation (rad). A positive pitch looks up in the engine's
    // convention, so the spawn tilt is negative: the terrain, not the sky.
    float yaw = 0.7f, pitch = -0.18f;
    float orbitYaw = 0.7f, orbitPitch = 0.35f;
    float orbitRadius = 26.0f;
    glm::vec3 focus{0.0f};                        // orbit pivot / fly position
    float speed = 24.0f;
    bool ortho = false;                           // Tab: projection toggle
    int selected = Block::kGrass;
    float sunAzimuth = 0.75f, sunElevation = 0.8f;
    bool wantBreak = false, wantPlace = false;
    bool showParticles = true;
    // Renderer features that --off can switch off before the first frame.
    bool showSky = true;
    bool showFog = true;
    float waterAlpha = 0.85f;
    bool doubleSided = false;                 // B: draw terrain two-sided (no back cull)
};

// A few well-known system fonts, tried in order; HUD text just no-ops if none
// are found, so a missing font never breaks the render.
extern const char* const kFontCandidates[];
extern const int kFontCandidateCount;

// Unit vector pointing from the scene toward the sun (elevation > 0 => upward).
glm::vec3 SunToward(const Input& in);

// `keepCursor` is the scripted-run override: with --freeze-at the view has to
// stay where --yaw/--pitch put it, and a captured pointer hands the last word
// to whoever is moving the mouse next to the window.
void ApplyCapture(GLFWwindow* win, Input& in, bool keepCursor = false);

void MouseCallback(GLFWwindow* win, double x, double y);
void MouseButtonCallback(GLFWwindow* win, int button, int action, int);
void ScrollCallback(GLFWwindow* win, double, double dy);

} // namespace voxel_terrain

#endif // VOXEL_TERRAIN_INPUT_H
