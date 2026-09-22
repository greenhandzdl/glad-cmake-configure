/**
 * @file main.cpp
 * @brief render_passes - N windows, each a for-loop-built chain of *different*
 *        RenderPass subclasses, all spinning off one shared CPU clock.
 *
 * Where multi_viewport stresses the PBR scene across contexts, this demo
 * stresses the render-pass extensibility surface directly: every window builds
 * its own gldx::Renderer and, inside View::Create, appends a ClearPass and then
 * one ShapePass *subclass* per row of a factory table (triangle / quad / lines /
 * points / a second triangle). The visible result - several distinct primitives
 * drawing side by side, in pass order, on every window - is the proof that:
 *
 *   * Deriving many RenderPass types and feeding them to one Renderer works, and
 *     a plain for loop over a table can assemble the whole chain.
 *   * Each window's pass chain, programs and VAO/VBO live on that window's own
 *     GL context and nowhere else (View.{h,cpp}); two windows run byte-identical
 *     creation code yet share zero GPU objects.
 *   * The windows are synchronised purely through CPU state: one background
 *     worker advances a std::atomic clock (SharedState.{h,cpp}) that every pass
 *     samples per frame, so the shapes rotate in lockstep across all contexts.
 *   * A screenshot burst freezes that clock first, so every window captures the
 *     exact same animation phase - pixel-diffable evidence of the sync.
 *
 * Controls: --windows N (default 3), --quit-after SECONDS for headless sweeps,
 * and --shot-at SEC --shot-prefix PATH to dump one PNG per window.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "SharedState.h"
#include "View.h"

using render_passes::Clamp;
using render_passes::kMaxWindows;
using render_passes::SharedState;
using render_passes::View;
using render_passes::Worker;

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"windows", "shot-at", "shot-prefix"},
                                 "render_passes");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const int total     = Clamp(flags.integer("windows", 3), 1, kMaxWindows);
    const double shotAt = flags.number("shot-at", -1.0);
    // An empty prefix would scatter "0.png" into the cwd; name the burst.
    std::string shotPrefix = flags.string("shot-prefix", "");
    if (shotAt >= 0.0 && shotPrefix.empty()) shotPrefix = "render_passes_";
    bool shotTaken = false;

    SharedState state;
    Worker worker(state);   // joins on scope exit; only then do the atomics die

    // Destruction order: Window::~Window fires OnDestroy, which reaches into its
    // View, so the Windows must die before the Views. Declared first (destroyed
    // last) and cleared explicitly below.
    std::vector<std::unique_ptr<gldx::win::Window>> windows;
    std::vector<std::unique_ptr<View>>              views;

    for (int i = 0; i < total; ++i) {
        auto view = std::make_unique<View>();
        view->index = i;
        view->total = total;
        view->state = &state;
        View* v = view.get();

        gldx::win::WindowDesc desc;
        desc.width     = 640;
        desc.height    = 420;
        desc.resizable = true;
        const std::string title = "gldx demo - render_passes [" + std::to_string(i + 1) +
                                  "/" + std::to_string(total) + "]";
        desc.title = title.c_str();   // only read during construction
        auto window = std::make_unique<gldx::win::Window>(desc);
        if (!window->Ok()) return 1;

        // Tile left to right so all windows are visible at once.
        window->SetPos({60.0 + i * 660.0, 120.0});

        window->OnCreate([v](gldx::win::Window&) { v->Create(); });
        window->OnFrame([v, i, shotAt, &shotTaken, shotPrefix](gldx::win::FrameInfo& info) {
            v->Frame(info);
            // One burst past the requested time: freeze the shared clock first,
            // then every window screenshots the identical phase. The last closes
            // the run.
            if (shotAt >= 0.0 && info.time >= shotAt && !shotTaken) {
                v->state->freeze.store(true, std::memory_order_relaxed);
                const std::string path = shotPrefix + std::to_string(i) + ".png";
                if (info.window->CaptureScreenshot(path))
                    std::printf("render_passes: captured %s\n", path.c_str());
                if (i + 1 == v->total) {
                    shotTaken = true;
                    info.window->Close();
                }
            }
        });
        window->OnDestroy([v](gldx::win::Window&) {
            v->Release();   // context-private frees, context still current
        });

        windows.push_back(std::move(window));
        views.push_back(std::move(view));
    }

    const int rc = gldx::win::App::Get().Run({flags.quitAfter()});

    windows.clear();   // fire OnDestroy while the views are definitely alive
    views.clear();
    return rc;
}
