/**
 * @file main.cpp
 * @brief texture_samplers - one Texture2D, two sampler policies, side by side.
 *
 * Texture2D and Sampler are separate RAII objects. The texture carries pixels
 * (Upload from a CPU Texture2DDesc, Stage A -> B like every other resource);
 * the sampler carries the *policy* - min/mag filter, wrap, LOD clamp, optional
 * shadow compare. Because they are decoupled, the same texture can be sampled
 * two ways by binding a different sampler object to the same texture unit between
 * draws. That is exactly what this demo shows: a tiny 32x32 procedural checker,
 * magnified to fill each half of the window, sampled NEAREST on the left (hard
 * texel blocks) and LINEAR on the right (smooth interpolation).
 *
 * There is no lighting / PBR / scene here: a hand-built two-quad VAO (pos + uv)
 * and an inline sampler2D program, so nothing but the texture path is exercised.
 * The low source resolution is the point - at 32x32 blown up to hundreds of
 * pixels, nearest and linear are unmistakably different. The pass itself, and
 * the VertexArray/GLBuffer composition it uses, live in SamplerPass.{h,cpp}.
 *
 * Controls: Esc quits, --quit-after SECONDS for headless.
 */

#include "gldx/core/Platform.h"

import gldx;
import gldxwin;
import gldxcli;

#include <memory>

#include "SamplerPass.h"

int main(int argc, char** argv) {
    const gldx::cli::Flags flags(argc, argv, {}, {}, "texture_samplers");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    gldx::win::WindowDesc desc;
    desc.title = "gldx demo - texture_samplers (Texture2D + Sampler)";
    gldx::win::Window window(desc);
    if (!window.Ok()) return 1;

    gldx::RenderContext::MarkAsRenderThread();

    gldx::Renderer renderer;
    renderer.Init();

    renderer.AddPass(std::make_unique<texture_samplers::SamplerPass>());

    window.OnFrame([&](const gldx::win::FrameInfo& info) {
        gldx::RenderFrame f;
        f.fbWidth     = info.fbWidth;
        f.fbHeight    = info.fbHeight;
        f.smoothedFps = info.smoothedFps;

        renderer.Render(f);
    });

    return gldx::win::App::Get().Run({flags.quitAfter()});
}
