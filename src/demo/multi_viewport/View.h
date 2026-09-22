#ifndef MULTI_VIEWPORT_VIEW_H
#define MULTI_VIEWPORT_VIEW_H

/**
 * @file View.h
 * @brief One window's worth of context-private everything: the renderer, the
 *        scene, and the per-frame draw. Built in that window's OnCreate (its
 *        context current) and freed in OnDestroy (same), so no object in here
 *        is ever alive on the wrong context.
 *
 * The CPU-side shared truth (orbit, phase, ticks, freeze) lives in
 * SharedState.h; a View only ever *samples* it, plus the drag callbacks that
 * write it back through the gldxwin input surface.
 */

#include "SharedState.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

import gldx;
import gldxwin;

namespace multi_viewport {

struct View {
    int          index  = 0;
    int          total  = 1;
    float        yawOffset   = 0.0f;   // this window's fixed share of the orbit
    float        pitchOffset = 0.0f;
    SharedState* state = nullptr;

    // Mouse-drag scratch (input callbacks run on the render thread inside the
    // frame loop, so these need no atomics).
    bool   dragging = false;
    double lastX = 0.0, lastY = 0.0;

    // GL objects: each window runs this identical code on its own context, so
    // every handle below names a *different* object from the other windows'.
    std::unique_ptr<gldx::Renderer>   renderer;
    // optional<expected<...>> so Release() can reset it: expected itself has
    // no empty-assignment, and the program must die while this context is current.
    std::optional<std::expected<gldx::ShaderProgram, std::string>> pbr;
    std::unique_ptr<gldx::LightBuffer> lights;
    std::unique_ptr<gldx::DebugDraw>   debug;
    std::unique_ptr<gldx::SpriteBatch> sprite;
    std::unique_ptr<gldx::Font>        font;
    std::unique_ptr<gldx::Texture2D>   white;
    gldx::Camera camera;

    // CPU scene description, rebuilt identically per view (deterministic).
    struct Item {
        gldx::Mesh        mesh;
        gldx::PbrMaterial material;
    };
    std::vector<std::unique_ptr<Item>> items;
    gldx::Scene scene;
    gldx::SceneNode* spinner = nullptr;   // the cube whose phase proves sync

    void Create();
    // Frees the context-private GL objects; called from OnDestroy with this
    // window's context still current - the teardown half of the contract.
    void Release();
    void Frame(const gldx::win::FrameInfo& info);

    // gldxwin input-surface callbacks (subscribed by main with the View
    // captured): press/release tracks the drag, motion orbits the SHARED
    // camera so all windows swing together while each keeps its own offset.
    void Button(gldx::win::MouseButton button, gldx::win::KeyAction action);
    void Drag(gldx::win::Vec2d pos);

private:
    // Deterministic content: ground, a 5x5 metallic/roughness sphere grid and
    // a central cube spinning with the shared phase. Identical inputs in every
    // view, so any pixel difference between windows must come from the camera
    // angle alone.
    void BuildScene();
    void DrawHud(const gldx::win::FrameInfo& info);
};

} // namespace multi_viewport

#endif // MULTI_VIEWPORT_VIEW_H
