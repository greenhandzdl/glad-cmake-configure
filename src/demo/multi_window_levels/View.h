#ifndef MULTI_WINDOW_LEVELS_VIEW_H
#define MULTI_WINDOW_LEVELS_VIEW_H

/**
 * @file View.h
 * @brief One window's worth of context-private everything, configured by its
 *        Profile. Built in that window's OnCreate (its context current) and
 *        freed in OnDestroy (same), so no GPU object is ever alive on the
 *        wrong context.
 *
 * A View is one of two shapes chosen by Profile::independent:
 *   * field window - a Renderer + GeometryPass + PBR program + LightBuffer +
 *     a Scene holding the shared CPU object field, drawn with a per-window
 *     camera. Its frustum culls the SAME field to a DIFFERENT visible count,
 *     which GeometryPass reports back through RenderFrame for the HUD.
 *   * island window - none of the above. Only a DebugDraw line overlay and a
 *     text HUD, driven off a manual glClear. It shares no scene, no pipeline
 *     and no clock with the field windows: the strongest demonstration that
 *     two GL contexts in one process can be genuinely unrelated.
 *
 * The animation phase is either sampled from the shared atomic (Sync) or
 * accumulated privately from per-frame dt (Async) - see Profiles.h.
 */

#include "Profiles.h"
#include "SharedState.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

import gldx;
import gldxwin;

namespace multi_window_levels {

struct View {
    int          index  = 0;
    int          total  = 1;
    Profile      profile;
    SharedState* state = nullptr;

    // Private async clock (also drives the island). Ignored for Sync windows,
    // which read state->syncPhase instead. Only the render thread touches it.
    double phase = 0.0;

    // ---- context-private GL objects (field windows) ----------------------
    std::unique_ptr<gldx::Renderer>    renderer;
    std::optional<std::expected<gldx::ShaderProgram, std::string>> pbr;
    std::unique_ptr<gldx::LightBuffer> lights;
    gldx::Camera camera;

    struct Item {
        gldx::Mesh        mesh;
        gldx::PbrMaterial material;
    };
    std::vector<std::unique_ptr<Item>> items;
    gldx::Scene scene;
    gldx::SceneNode* spinner = nullptr;   // cube whose angle proves sync vs async

    // ---- context-private GL objects (every window, incl. island) ---------
    std::unique_ptr<gldx::DebugDraw>   debug;
    std::unique_ptr<gldx::SpriteBatch> sprite;
    std::unique_ptr<gldx::Font>        font;
    std::unique_ptr<gldx::Texture2D>   white;

    void Create();
    void Release();
    void Frame(const gldx::win::FrameInfo& info);

private:
    void BuildField();      // shared CPU object field at this window's LOD tier
    void DrawHud(const gldx::win::FrameInfo& info,
                 int visible, int totalNodes);
};

} // namespace multi_window_levels

#endif // MULTI_WINDOW_LEVELS_VIEW_H
