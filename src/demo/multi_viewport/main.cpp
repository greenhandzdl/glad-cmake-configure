/**
 * @file main.cpp
 * @brief multi_viewport - N windows, one shared scene, one render thread.
 *
 * The stress this demo exists to prove out is the exact contract gldxwin
 * promises but no earlier demo exercised: App::Run drives *several* windows in
 * one pass, and each window owns its own GL context. One GLFW window = one
 * context = one private object namespace, so nothing GPU-side may be shared
 * between the windows here:
 *
 *   * Every GL resource (programs, VAOs/VBOs, textures, FBOs, UBOs) is created
 *     inside that window's OnCreate - gldxwin makes its context current first -
 *     and destroyed in OnDestroy before the window dies. Two windows running
 *     the very same upload code build two fully independent object sets that
 *     happen to hold identical CPU-side inputs. That context-private world lives
 *     in View.{h,cpp}.
 *   * The *scene* is shared, but only as CPU state: a deterministic builder
 *     feeds each view, plus one SharedState of atomics that the background
 *     worker thread and the render thread both touch (SharedState.{h,cpp}).
 *     That shared truth is what makes the windows "synchronised": the spinning
 *     cube shows the same phase in every window on every frame, and a drag in
 *     any window orbits all of them (each keeps its fixed angular offset).
 *   * Thread safety is probed from both sides: a worker thread continuously
 *     advances the animation clock and auto-orbit through std::atomics only,
 *     while every GL call stays on the render thread and on the context the
 *     loop just made current (asserted per frame in View::Frame). Run the
 *     whole demo under TSan and the worker/render hand-off must stay clean.
 *
 * What is left here is the wiring only: build the windows, subscribe the input
 * callbacks, and drive App::Run.
 *
 * Controls: drag in any window orbits the shared camera, Esc closes *that*
 * window (the run ends when the last one goes), --quit-after SECONDS for
 * headless sweeps, --windows N (default 3), and
 * --shot-at SEC --shot-prefix PATH dumps one PNG per window via
 * Window::CaptureScreenshot so a sweep can diff the views pixel-wise.
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

using multi_viewport::Clamp;
using multi_viewport::kMaxWindows;
using multi_viewport::kPi;
using multi_viewport::SharedState;
using multi_viewport::View;
using multi_viewport::Worker;

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"windows", "shot-at", "shot-prefix"},
                                 "multi_viewport");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const int total      = Clamp(flags.integer("windows", 3), 1, kMaxWindows);
    const double shotAt  = flags.number("shot-at", -1.0);
    // Empty prefix would write "0.png" into the cwd; give the burst a name.
    std::string shotPrefix = flags.string("shot-prefix", "");
    if (shotAt >= 0.0 && shotPrefix.empty()) shotPrefix = "multi_viewport_";
    bool shotTaken = false;

    SharedState state;
    Worker worker(state);   // joins on scope exit; only then do the atomics die

    // Destruction order matters: Window::~Window fires OnDestroy, which
    // touches its View, so the Windows must die before the Views do. They are
    // declared first (destroyed last), hence the explicit clear() below.
    std::vector<std::unique_ptr<gldx::win::Window>> windows;
    std::vector<std::unique_ptr<View>>              views;

    for (int i = 0; i < total; ++i) {
        auto view = std::make_unique<View>();
        view->index       = i;
        view->total       = total;
        view->state       = &state;
        view->yawOffset   = i * (2.0f * kPi / static_cast<float>(total));
        view->pitchOffset = (i % 2 == 0 ? 0.12f : -0.28f) + (i / 2) * 0.10f;
        View* v = view.get();

        gldx::win::WindowDesc desc;
        desc.width     = 640;
        desc.height    = 420;
        desc.resizable = true;
        const std::string title = "gldx demo - multi_viewport [" + std::to_string(i + 1) +
                                  "/" + std::to_string(total) + "]";
        desc.title = title.c_str();   // only read during construction
        auto window = std::make_unique<gldx::win::Window>(desc);
        if (!window->Ok()) return 1;

        // Tile the windows left to right so all of them are on screen at once.
        window->SetPos({60.0 + i * 660.0, 120.0});
        // Dragging in ANY window nudges the SHARED orbit, so all windows swing
        // together while each keeps its own offset. The callbacks run on the
        // render thread inside the frame loop; the atomics are for the worker.
        window->OnMouseButton([v](gldx::win::Window&, gldx::win::MouseButton button,
                                   gldx::win::KeyAction action, int) {
            v->Button(button, action);
        });
        window->OnCursor([v](gldx::win::Window&, gldx::win::Vec2d pos) { v->Drag(pos); });

        window->OnCreate([v](gldx::win::Window&) { v->Create(); });
        window->OnFrame([v, i, shotAt, &shotTaken, shotPrefix](gldx::win::FrameInfo& info) {
            v->Frame(info);
            // One burst past the requested time: freeze the shared clock first
            // (the worker stops advancing phase/orbit/ticks), then every window
            // in this same loop pass screenshots the identical scene state from
            // its own angle - pixel-diffable proof of the sync. The last window
            // ends the run.
            if (shotAt >= 0.0 && info.time >= shotAt && !shotTaken) {
                v->state->freeze.store(true, std::memory_order_relaxed);
                const std::string path = shotPrefix + std::to_string(i) + ".png";
                if (info.window->CaptureScreenshot(path))
                    std::printf("multi_viewport: captured %s\n", path.c_str());
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

    // Every Window destructor calls into its View (OnDestroy), so tear the
    // windows down while the views are definitely still alive, before main's
    // locals unwind in reverse order.
    windows.clear();
    views.clear();
    return rc;
}
