/**
 * @file main.cpp
 * @brief multi_window_levels - graded windows that explore the full multi-window
 *        design space, not just the synchronised corner.
 *
 * multi_viewport proves one specific point: several windows sharing a single
 * atomic clock and one identical scene, differing only by a fixed camera
 * offset. This demo asks the opposite questions - what happens when windows do
 * NOT share those things - because the engine's layering permits it and users
 * need to see the options laid out. Each window gets a Profile (Profiles.h)
 * that varies four orthogonal axes independently:
 *
 *   Sync vs Async  - the two SYNC windows sample the one SharedState atomic
 *     phase, so the spinner cube shows the identical angle in both every frame;
 *     the ASYNC windows ignore SharedState entirely and integrate their own
 *     phase from per-frame dt * rate, so they drift apart. The HUD prints each
 *     window's phase, so the agreement (and the divergence) is readable.
 *   Different culling - every field window aims its own camera frustum at the
 *     SAME shared object field. Because the eye point, FOV and far plane differ
 *     per window, GeometryPass culls to a different visible/total per window -
 *     literally "this window shows it, that one does not", counted on screen.
 *     The rear-guard window is the pointed case: same clock as flagship, but
 *     swung 180 degrees, so it keeps the spheres flagship culled and vice versa.
 *   Different tiers - LOD tessellation and view distance step down the table
 *     from the smooth, long-range flagship to the chunky, short-range scout.
 *   Independent context - the island window shares NOTHING: no scene, no
 *     pipeline, no clock, not even a Renderer. It clears its own framebuffer
 *     and draws a DebugDraw wireframe, proving two GL contexts in one process
 *     and one render thread can be genuinely unrelated.
 *
 * Everything still honours the gldxwin contract: one render thread, each
 * window's GL resources built in its OnCreate and freed in its OnDestroy with
 * its own context current, cross-window sharing limited to CPU atomics.
 *
 * Controls: Esc closes *that* window (the run ends when the last goes),
 * --quit-after SECONDS for headless sweeps, --windows N (default 5, one per
 * profile), and --shot-at SEC --shot-prefix PATH dumps one PNG per window.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Profiles.h"
#include "SharedState.h"
#include "View.h"

using multi_window_levels::Clock;
using multi_window_levels::kMaxProfiles;
using multi_window_levels::Profile;
using multi_window_levels::ProfileTable;
using multi_window_levels::SharedState;
using multi_window_levels::View;
using multi_window_levels::Worker;

namespace {
template <class T>
constexpr T Clamp(T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {"windows", "shot-at", "shot-prefix"},
                                 "multi_window_levels");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    const auto& profiles = ProfileTable();
    const int total     = Clamp(flags.integer("windows", kMaxProfiles), 1,
                                static_cast<int>(profiles.size()));
    const double shotAt = flags.number("shot-at", -1.0);
    std::string shotPrefix = flags.string("shot-prefix", "");
    if (shotAt >= 0.0 && shotPrefix.empty()) shotPrefix = "multi_window_levels_";
    bool shotTaken = false;

    SharedState state;
    Worker worker(state);   // joins on scope exit; only then do the atomics die

    // Windows are declared before views so they are destroyed after them: the
    // Window destructor fires OnDestroy, which reaches into its View. The
    // explicit clear() at the end keeps that ordering regardless of scope exit.
    std::vector<std::unique_ptr<gldx::win::Window>> windows;
    std::vector<std::unique_ptr<View>>              views;

    for (int i = 0; i < total; ++i) {
        const Profile& profile = profiles[static_cast<std::size_t>(i)];
        auto view = std::make_unique<View>();
        view->index = i;
        view->total = total;
        view->state = &state;
        view->profile    = profile;
        view->phase      = profile.start;   // async/island start; sync overwrites
        View* v = view.get();

        gldx::win::WindowDesc desc;
        desc.width     = 640;
        desc.height    = 420;
        desc.resizable = true;
        const std::string title =
            std::string("gldx demo - multi_window_levels [") + profile.name + "]";
        desc.title = title.c_str();   // only read during construction
        auto window = std::make_unique<gldx::win::Window>(desc);
        if (!window->Ok()) return 1;

        // A 3-wide tiled grid so all graded windows stay on screen at once.
        window->SetPos({40.0 + (i % 3) * 660.0, 40.0 + (i / 3) * 480.0});

        window->OnCreate([v](gldx::win::Window&) { v->Create(); });
        window->OnFrame([v, i, shotAt, &shotTaken, shotPrefix](gldx::win::FrameInfo& info) {
            v->Frame(info);
            // One burst past the requested time: freeze the SHARED clock so the
            // two sync windows are caught at the same phase (the async and
            // island windows keep running on their own - by design). Each
            // window screenshots its own tier; the last one ends the run.
            if (shotAt >= 0.0 && info.time >= shotAt && !shotTaken) {
                v->state->freeze.store(true, std::memory_order_relaxed);
                const std::string path = shotPrefix + std::to_string(i) + ".png";
                if (info.window->CaptureScreenshot(path))
                    std::printf("multi_window_levels: captured %s\n", path.c_str());
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

    windows.clear();   // tear down windows while their views are still alive
    views.clear();
    return rc;
}
