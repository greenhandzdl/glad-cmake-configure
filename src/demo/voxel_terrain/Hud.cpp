// voxel_terrain demo — HUD render pass implementation (see Hud.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Hud.h"

#include <glm/glm.hpp>

namespace voxel_terrain {

using Hud = gldx::Hud;

void VoxelHudPass::Build(gldx::RenderFrame& /*f*/) {
    // The frame callback formats the counters into `text`; this only decides
    // where they land. 20 px because the panel is the demo's main readout and
    // it competes with a world, not with another wall of numbers. AddBlock
    // splits the newline-joined string, so main.cpp keeps writing one snprintf
    // and the engine still gets one line per row.
    hud.BeginSection(Hud::Anchor::TopLeft, 20.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.35f));
    hud.AddBlock(text, Hud::kFg);

    // ---- controls, in their own panel so the counters stay readable ---------
    // These used to be the tail of the same string, inside the same box, which
    // is why that box had a hard-coded width: the keys are constant, so they
    // belong in a panel that never changes size, next to the state they explain
    // (printed above as "next F -> orbit", i.e. what the key does from here).
    //
    // ASCII 32..126 only - the baked atlas covers exactly that range, so a
    // typographic arrow would render as a missing glyph rather than an arrow.
    hud.BeginSection(Hud::Anchor::BottomLeft, 12.0f,
                     glm::vec4(0.0f, 0.0f, 0.0f, 0.45f));
    hud.AddLines({
        "WASD fly  Space/Ctrl up/down  Shift slow  mouse look",
        "LMB break  RMB place  1..8 block  Tab proj  scroll zoom",
        "F camera  P particles  B 2-sided  [ ] azimuth  - = elevation",
        "Esc release  X quit",
    });

    // A camera that can break blocks aims at something, so it gets the plus;
    // the orbit camera has nothing to aim. HudPass::Execute draws the marker
    // and closes the profiler frame on every path out of here.
    hud.SetMarker(crosshair ? Hud::Marker::Plus : Hud::Marker::None);
}

} // namespace voxel_terrain
