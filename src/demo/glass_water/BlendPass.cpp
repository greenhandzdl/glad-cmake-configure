// glass_water demo — the layered transparency pass (see BlendPass.h).
#include "gldx/core/Platform.h"

import gldx;

#include "BlendPass.h"
#include "Shaders.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include <glm/glm.hpp>

namespace glass_water {

bool BlendPass::Init() {
    gldx::RenderContext::AssertRenderThread("BlendPass::Init");
    auto made = gldx::ShaderProgram::CreateFromSource(kVertex, kFragment);
    if (!made) {
        std::fprintf(stderr, "glass_water shader error:\n%s\n", made.error().c_str());
        return false;
    }
    prog_ = std::move(*made);
    // Sampler-unit uniforms live on the program object, so the one texture slot
    // the whole scene shares is pointed once instead of per draw.
    prog_.Set("uTex", static_cast<int>(gldx::texunit::albedo));
    return true;
}

void BlendPass::ApplyBlendState(BlendMode mode, bool writeDepth, bool cull) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(writeDepth ? GL_TRUE : GL_FALSE);
    glEnable(GL_BLEND);

    // The alpha *channel* is held open on purpose: dst alpha = 0*src + 1*dst
    // leaves the target's alpha exactly as BeginScene cleared it. Without the
    // "Separate", every blended layer would also blend the destination alpha, and
    // a window that carries an alpha plane starts out opaque and ends up letting
    // the desktop through - a bug that only appears on some platforms and then
    // looks like a driver glitch rather than a missing line of state.
    switch (mode) {
        case BlendMode::Straight:
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            break;
        case BlendMode::Premultiplied:
            glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            break;
        case BlendMode::Additive:
            // Light only ever adds. Nothing is subtracted from the destination, so
            // the compositing is commutative and the draw order stops mattering -
            // which is why this mode is the control group for the sorting switch:
            // flip to it and the very artefact sorting exists to prevent vanishes.
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
            break;
    }

    // A transparent sheet is seen from both sides by construction (you look
    // through it), so culling the back of a blended object is only legitimate for
    // the closed shells, and only while culling is on.
    if (cull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
    else glDisable(GL_CULL_FACE);
}

void BlendPass::DrawItem(const gldx::ShaderProgram& p, const Item& item,
                         const Sortable* prim) {
    const auto& meshes = settings.scene->meshes();
    const auto& textures = settings.scene->textures();
    if (item.mesh < 0 || static_cast<std::size_t>(item.mesh) >= meshes.size()) return;
    const gldx::Mesh& m = meshes[static_cast<std::size_t>(item.mesh)];
    if (!m.valid()) return;

    const Surface& s = item.surf;
    p.Set("uModel", item.model);
    p.Set("uNormalMatrix", item.normalMatrix);
    p.Set("uAlbedo", s.albedo);
    // The BRDF knobs. Note that ior / roughness / absorption are the whole
    // material description - there is no "transparency shader" branch below, and
    // no per-layer set of uniforms either: the lake and the pane differ by four
    // numbers, not by a program.
    p.Set("uIor", s.ior);
    p.Set("uRoughness", s.roughness);
    p.Set("uAbsorption", s.absorption);
    p.Set("uPathLen", s.pathLen);
    p.Set("uBaseAlpha", s.baseAlpha);
    p.Set("uFresnelToAlpha", s.fresnelToAlpha);
    p.Set("uCoreToAlpha", s.coreToAlpha);
    p.Set("uAlphaCutoff", s.alphaCutoff);
    p.Set("uIridescence", s.iridescence);
    p.Set("uEmissive", s.emissive);
    // The multiply that keeps rgb and alpha a matched pair, or the absence of it:
    // 1 only when the blend equation about to run is (ONE, 1-SRC_ALPHA), which
    // adds the source untouched and therefore expects it already weighted.
    p.Set("uPremultiply", premul_ ? 1.0f : 0.0f);
    p.Set("uLightMix", s.lightMix);
    p.Set("uUvScale", item.uvScale);
    // waveAmp doubles as a "this item is the sea" flag; the amplitude itself is
    // the live dial, so `[` `]` retunes the whole basin without touching the
    // scene table.
    p.Set("uWaveAmp", s.waveAmp > 0.0f ? settings.wave * s.waveAmp : 0.0f);
    if (item.tex >= 0 && static_cast<std::size_t>(item.tex) < textures.size())
        textures[static_cast<std::size_t>(item.tex)].Bind(gldx::texunit::albedo);

    const auto drawOnce = [&]() {
        if (prim && prim->tile >= 0 && item.tiles) {
            m.DrawRange((*item.tiles)[static_cast<std::size_t>(prim->tile)].range);
        } else {
            m.Draw();   // whole mesh; for the sheet that is all 36 tiles in
                        // declaration order, which is the unsorted comparison
        }
        ++totalDraws_;
    };

    const bool shell = item.shellTwoPass && settings.shellTwoPass;
    if (shell) {
        // A closed transparent body has two surfaces on every view ray and no
        // amount of object sorting can interleave them, so the item pays for two
        // submissions: the far skin (front faces culled away) and then the near
        // skin over the top of it. Neither writes depth, so both survive.
        //
        // Note the quiet dependency on culling: with CULL_FACE off there is no
        // "far skin only" state left to ask for, and the shell is simply drawn
        // twice. `C` therefore visibly changes the shells, which is exactly the
        // coupling this demo wants you to be able to see.
        glCullFace(GL_FRONT);
        drawOnce();
        glCullFace(GL_BACK);
        drawOnce();
    } else if (item.doubleSided) {
        glDisable(GL_CULL_FACE);
        drawOnce();
        if (settings.cull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
    } else {
        drawOnce();
    }
}

void BlendPass::Execute(gldx::RenderFrame& f) {
    gldx::RenderContext::AssertRenderThread("BlendPass::Execute");
    const Scene* scene = settings.scene;
    if (!scene || scene->items().empty() || !prog_.valid() || f.fbWidth <= 0) return;

    // Open the scene target. The engine's own BeginSceneTarget is module-internal,
    // so a demo that replaces GeometryPass opens the chain itself: linear HDR into
    // the MSAA target, and PostProcessPass (next in the list) closes and composites
    // it. Without a chain the same draws go at the window instead.
    if (f.post) {
        f.post->Resize(f.fbWidth, f.fbHeight, 4);
        f.post->BeginScene();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    blendDraws_ = 0;
    totalDraws_ = 0;
    order_.clear();
    sortBuffer_.clear();

    const glm::vec3 eye = f.camera ? f.camera->Position() : glm::vec3(0.0f);
    gldx::ShaderProgram& p = prog_;
    p.Use();
    // Frame constants: identical for every item, so they go once.
    p.Set("uViewProj", f.viewProj);
    p.Set("uEye", eye);
    p.Set("uOrthographic", settings.orthographic ? 1.0f : 0.0f);
    p.Set("uCamForward", settings.camForward);
    p.Set("uSunDir", glm::normalize(settings.sunToward));
    p.Set("uSunColor", kSunColor);
    // The same three bands the dome's texture was painted from, so a reflection
    // is a reflection *of that sky* and not of a plausible tint.
    p.Set("uSkyZenith", kSkyZenith);
    p.Set("uSkyHorizon", kSkyHorizon);
    p.Set("uSkyNadir", kSkyNadir);
    p.Set("uTime", settings.time);
    // Everything up to and including the cutout layer is drawn unblended, so its
    // alpha is never read by hardware and the rgb must go out as plain radiance.
    premul_ = false;

    glFrontFace(GL_CCW);
    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    const auto& items = scene->items();

    // ---- layer 1: the sky, which is not allowed to occlude anything ----------
    // No depth test and no depth write: the dome is a backdrop painted first, and
    // every later layer starts from a clean depth buffer. Culling the front faces
    // keeps only the inside of the sphere, which is the shell the camera is in.
    {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
        for (const Item& item : items)
            if (item.layer == Layer::Sky) DrawItem(p, item, nullptr);
    }

    // ---- layer 2: opaque, the only layer that fills the depth buffer ---------
    {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        if (settings.cull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
        else glDisable(GL_CULL_FACE);
        for (const Item& item : items)
            if (item.layer == Layer::Opaque) DrawItem(p, item, nullptr);
    }

    // ---- layer 3: cutout, alpha-tested rather than blended -------------------
    // `discard` costs the fragment both its colour and its depth, so a hole is a
    // real gap in the depth buffer and the layer keeps writing depth and keeps
    // needing no order. Grouping it with the opaque pass instead of the blended
    // one is the whole reason a punched-out fence is cheaper and more correct than
    // a translucent one. Only the cull mode changes, because these are single
    // quads that have to read from both sides.
    {
        glDisable(GL_CULL_FACE);
        for (const Item& item : items)
            if (item.layer == Layer::Cutout) DrawItem(p, item, nullptr);
        if (settings.cull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
    }

    // ---- layer 4: the blend layer, the one this demo is about ----------------
    ApplyBlendState(settings.blend, settings.blendWritesDepth, settings.cull);
    // The fragment stage and the blend unit are one contract written in two
    // places: whichever multiplies the layer's radiance by its coverage, exactly
    // one of them may. Premultiplied asks the shader to do it, Straight asks the
    // fixed-function unit, and Additive asks neither to attenuate at all. So the
    // instructive pairing is not "straight is broken" - with a scalar multiply it
    // produces the identical frame, which is why `4` between those two is a
    // pixel-for-pixel no-op and worth cmp-ing as a check on this reasoning. What
    // actually breaks is a *mismatched* pair: the multiply dropped on both sides
    // dumps each layer's full radiance on top of the (1 - a) of background it just
    // let through and the stack goes white; kept on both sides it is an a-squared
    // falloff that erases the far-shore mirror band first.
    premul_ = settings.blend == BlendMode::Premultiplied;
    if (settings.sortBackToFront) {
        // Every sortable *primitive*, not every item: the water sheet contributes
        // its 36 tiles and each one competes for its own place in the sequence
        // against the panes, the bubble and the slab. Sorting the sheet as a
        // single object is what leaves half of the scene mis-composited, because
        // "the sheet's centre" is 12 units behind the panes while its near edge is
        // 30 units in front of them.
        for (std::size_t i = 0; i < items.size(); ++i) {
            const Item& item = items[i];
            if (item.layer != Layer::Blend) continue;
            if (item.tiles && !item.tiles->empty()) {
                for (std::size_t t = 0; t < item.tiles->size(); ++t) {
                    const Tile& tile = (*item.tiles)[t];
                    sortBuffer_.push_back({static_cast<int>(i), static_cast<int>(t), tile.center});
                }
            } else {
                sortBuffer_.push_back({static_cast<int>(i), -1, item.center});
            }
        }
        std::sort(sortBuffer_.begin(), sortBuffer_.end(),
                  [eye](const Sortable& a, const Sortable& b) {
                      // Squared distance, largest first: far before near, so each
                      // layer attenuates what it drew before rather than the
                      // other way round. glm::distance2 would do, but that is a
                      // GTX extension and this project stays on core + gtc.
                      const glm::vec3 da = a.center - eye;
                      const glm::vec3 db = b.center - eye;
                      return glm::dot(da, da) > glm::dot(db, db);
                  });
        order_.reserve(sortBuffer_.size());
        for (const Sortable& s : sortBuffer_) {
            const Item& item = items[static_cast<std::size_t>(s.item)];
            const glm::vec3 d = s.center - eye;
            order_.push_back({item.label, std::sqrt(glm::dot(d, d)), s.tile});
            DrawItem(p, item, &s);
            ++blendDraws_;
        }
    } else {
        // Sorting off: the scene table's declaration order, and the sheet as one
        // submission. The water was declared before the panes, so it goes down
        // first and every blended object then lands on top of it - the classic
        // "the layering is wrong but nothing is missing" frame.
        for (const Item& item : items) {
            if (item.layer != Layer::Blend) continue;
            DrawItem(p, item, nullptr);
            ++blendDraws_;
        }
    }

    // The state read-back happens here rather than being copied from the calls
    // above, so the HUD reports what the driver holds, not the intention.
    {
        GLint v = 0;
        glGetIntegerv(GL_DEPTH_TEST, &v);       glState_.depthTest = v != 0;
        glGetIntegerv(GL_DEPTH_WRITEMASK, &v);  glState_.depthWrite = v != 0;
        glGetIntegerv(GL_BLEND, &v);            glState_.blend = v != 0;
        glGetIntegerv(GL_CULL_FACE, &v);        glState_.cull = v != 0;
        glGetIntegerv(GL_BLEND_SRC_RGB, &v);    glState_.srcRGB = static_cast<int>(v);
        glGetIntegerv(GL_BLEND_DST_RGB, &v);    glState_.dstRGB = static_cast<int>(v);
    }

    // ---- layer 5: additive glow ----------------------------------------------
    // Drawn last, unsorted, and deliberately outside the sort: addition is
    // commutative, so stacking three lamps in any order gives the same frame. The
    // price is that nothing can be composited *over* a glow, so a light that has
    // to sit behind a translucent panel belongs in the Blend layer instead.
    if (settings.glow) {
        // SRC_ALPHA does the weighting for this layer, so the shader must hand
        // over unweighted radiance again - otherwise the lamps arrive
        // double-attenuated and the bloom has nothing bright left to key on.
        premul_ = false;
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
        glDepthMask(GL_FALSE);
        if (settings.cull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
        for (const Item& item : items) {
            if (item.layer != Layer::Glow) continue;
            DrawItem(p, item, nullptr);
            ++blendDraws_;
        }
    }

    // ---- hand the pipeline back in the state Renderer::Init() left it ---------
    // Every later pass (PostProcess, the HUD's SpriteBatch) assumes the defaults,
    // so this restore is part of the pass's contract, not a courtesy.
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    f.visibleCount = blendDraws_;
    f.totalNodes = static_cast<int>(items.size());
}

} // namespace glass_water
