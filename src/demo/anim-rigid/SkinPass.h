// The one render pass: floor + skinned character, drawn straight at the window.
//
// No PostProcessChain - this demo has no HDR/bloom to composite and the shaders encode
// their own output. NOTE: names gldx types, so include AFTER `import gldx;`.
#pragma once

// The standard headers this header actually uses. Relying on the including TU to
// have pulled them in is exactly how this file came to compile on clang (whose libc++
// transitively includes <memory>/<string>) and fail on libstdc++ and MSVC - see the
// v1.7.3 release note: the demo had never been built on Linux/Windows.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <utility>
#include <vector>

#include "Shaders.h"
#include "Character.h"
#include "Scene.h"
#include "View.h"

namespace anim {

class SkinPass : public gldx::RenderPass {
public:
    SkinPass(Character& character, View& view)
        : RenderPass("Skin"), character_(character), view_(view) {
        if (auto skin = gldx::ShaderProgram::CreateFromSource(anim_rigid::shaders::kSkinVertex,
                                                             anim_rigid::shaders::kSkinFragment)) {
            skin_.emplace(std::move(*skin));
        } else {
            std::fprintf(stderr, "[anim-rigid] skin shader: %s\n", skin.error().c_str());
        }
        if (auto ground = gldx::ShaderProgram::CreateFromSource(anim_rigid::shaders::kGroundVertex,
                                                               anim_rigid::shaders::kGroundFragment)) {
            ground_.emplace(std::move(*ground));
        } else {
            std::fprintf(stderr, "[anim-rigid] ground shader: %s\n", ground.error().c_str());
        }
        // 80 x 80 m: far enough that the distance fade hides the edge, cheap
        // enough that the whole floor is two triangles.
        groundMesh_.Upload(gldx::GeometryFactory::Plane(40.0f));
    }

    void Execute(gldx::RenderFrame& f) override {
        gldx::RenderContext::AssertRenderThread("SkinPass::Execute");
        // Own the state instead of inheriting it: gldx::SpriteBatch::End() leaves
        // GL_DEPTH_TEST and GL_CULL_FACE disabled (it only restores blending), and
        // the HUD pass drew last frame. Inheriting that is subtle, not loud - the
        // character renders with its own back faces and interior cloth showing
        // through, which reads as "the skinning is broken" at exactly the frame
        // where the HUD is enabled. Each pass in this repo establishes what it
        // draws with; this one draws 3D, so it asks for the 3D state.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
        glDisable(GL_BLEND);
        const glm::vec3 clear = DisplayColor(kSkyColor * 0.55f);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(clear.r, clear.g, clear.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (f.fbWidth <= 0 || f.fbHeight <= 0) return;

        const glm::vec3 eye = f.camera ? f.camera->Position() : glm::vec3(0.0f, 1.0f, 4.0f);
        int draws = 0;

        if (view_.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        if (view_.ground && ground_ && groundMesh_.valid()) {
            ground_->Use();
            ground_->Set("uViewProj", f.viewProj);
            ground_->Set("uCamPos", eye);
            ground_->Set("uSunDir", glm::normalize(kSunDir));
            ground_->Set("uSunColor", kSunColor);
            ground_->Set("uSunIntensity", kSunIntensity);
            ground_->Set("uSkyColor", kSkyColor);
            ground_->Set("uGroundColor", kGroundColor);
            ground_->Set("uCharPos", glm::vec3(0.0f));
            ground_->Set("uScroll", view_.scroll);
            groundMesh_.Draw();
            ++draws;
        }

        const gldx::RiggedModel& model = character_.model;
        if (skin_ && model.ready() && !character_.palette.empty()) {
            skin_->Use();
            skin_->Set("uViewProj", f.viewProj);
            skin_->Set("uCamPos", eye);
            skin_->Set("uSunDir", glm::normalize(kSunDir));
            skin_->Set("uSunColor", kSunColor);
            skin_->Set("uSunIntensity", kSunIntensity);
            skin_->Set("uSkyColor", kSkyColor);
            skin_->Set("uBounceColor", kBounceColor);
            // `GLDX_DRAWLOW`: the lowest skinned vertex computed from the palette that is
            // ABOUT to be uploaded - not from the one the report measures. Every height
            // probe so far read `character.palette` at a different moment and disagreed
            // with the picture; this one is taken at the draw itself.
            // `GLDX_SEATLOW`: where the SEAT (hips + skirt) is, not the lowest point of
            // the whole body. A W-sit is supposed to put the hips on the floor; if the
            // grounding pass is satisfied by a toe or the tail, the character visually
            // hovers while every "lowest vertex" reading says 0.
            if (std::getenv("GLDX_SEATLOW") && !character_.probeVerts.empty()) {
                static int shown = 0;
                if (shown++ < 2) {
                    std::vector<int> allowed;
                    if (character_.rig.hips >= 0) allowed.push_back(character_.rig.hips);
                    const int si = static_cast<int>(gldx::RigChainKind::Skirt);
                    for (int k = 0; k < character_.rig.chains[si].count; ++k)
                        allowed.push_back(character_.rig.chains[si].bones[k]);
                    float low = 1.0e9f;
                    for (const gldx::SkinnedVertex& v : character_.probeVerts) {
                        bool ok = false;
                        for (int i = 0; i < 4; ++i) {
                            if (v.boneWeight[i] <= 0.0f) continue;
                            const int bi = static_cast<int>(v.boneIndex[i]);
                            if (std::find(allowed.begin(), allowed.end(), bi) != allowed.end()) {
                                ok = true; break;
                            }
                        }
                        if (!ok) continue;
                        glm::vec4 q(0.0f);
                        for (int i = 0; i < 4; ++i) {
                            if (v.boneWeight[i] <= 0.0f) continue;
                            const std::size_t bi = v.boneIndex[i];
                            if (bi < character_.palette.size())
                                q += v.boneWeight[i] *
                                     (character_.palette[bi] * glm::vec4(v.position, 1.0f));
                        }
                        low = std::min(low, q.y);
                    }
                    std::fprintf(stderr, "[seatlow] seat lowest y = %.4f m\n", (double)low);
                }
            }
            // `GLDX_ALLDRAWLOW`: lowest vertex of ANY bone, at the draw call. Sit hovered
            // visually while `GLDX_SEATLOW` (measured before ApplyLift) reported 0.000 -
            // the same "two palettes" trap as before, so this one is taken where it
            // counts.
            if (std::getenv("GLDX_ALLDRAWLOW") && !character_.probeVerts.empty()) {
                static int shown = 0;
                if (shown++ < 2) {
                    float low = 1.0e9f;
                    int lowBone = -1;
                    for (const gldx::SkinnedVertex& v : character_.probeVerts) {
                        glm::vec4 q(0.0f);
                        int dom = -1;
                        float bestW = 0.0f;
                        for (int i = 0; i < 4; ++i) {
                            if (v.boneWeight[i] <= 0.0f) continue;
                            const std::size_t bi = v.boneIndex[i];
                            if (bi < character_.palette.size())
                                q += v.boneWeight[i] *
                                     (character_.palette[bi] * glm::vec4(v.position, 1.0f));
                            if (v.boneWeight[i] > bestW) {
                                bestW = v.boneWeight[i];
                                dom = static_cast<int>(bi);
                            }
                        }
                        if (q.y < low) { low = q.y; lowBone = dom; }
                    }
                    const glm::mat4 hp = character_.palette[(std::size_t)character_.rig.hips];
                    for (int kk = 0; kk < gldx::kRigSides; ++kk) {
                        const int lb[3] = {character_.rig.hips, character_.rig.legLow[kk],
                                           character_.rig.foot[kk]};
                        std::fprintf(stderr,
                                     "[bonepos] %s hips=%.3f knee%d=%.3f foot%d=%.3f head=%.3f\n",
                                     kk == 0 ? "L" : "R",
                                     (double)character_.palette[(std::size_t)lb[0]][3][1], kk,
                                     (double)character_.palette[(std::size_t)lb[1]][3][1], kk,
                                     (double)character_.palette[(std::size_t)lb[2]][3][1],
                                     (double)character_.palette[(std::size_t)character_.rig.head][3][1]);
                    }
                    std::fprintf(stderr, "[alldraw] hips y=%.4f m | lowest=%.4f m on bone %d '%s'\n",
                                 (double)hp[3][1], (double)low, lowBone,
                                 lowBone >= 0
                                     ? character_.model.skeleton().bones()[(std::size_t)lowBone].name.c_str()
                                     : "?");
                }
            }
            if (std::getenv("GLDX_DRAWLOW") && !character_.probeVerts.empty()) {
                static int shown = 0;
                if (shown++ < 2) {
                    float low = 1.0e9f;
                    for (const gldx::SkinnedVertex& v : character_.probeVerts) {
                        glm::vec4 q(0.0f);
                        for (int i = 0; i < 4; ++i) {
                            if (v.boneWeight[i] <= 0.0f) continue;
                            const std::size_t bi = v.boneIndex[i];
                            if (bi < character_.palette.size())
                                q += v.boneWeight[i] *
                                     (character_.palette[bi] * glm::vec4(v.position, 1.0f));
                        }
                        low = std::min(low, q.y);
                    }
                    std::fprintf(stderr, "[drawlow] lowest drawn vertex y = %.4f m\n",
                                 (double)low);
                }
            }
            model.BindPalette(*skin_, character_.palette);

            // Every sampler must point at a complete texture on every draw, dummy
            // or not: an active sampler with nothing behind it makes Apple's driver
            // drop the whole call (the failure looks like "the model vanished").
            // AlbedoOrFallback() and friends are that guarantee, so this loop has
            // no null case to handle.
            // ---- per-mesh material binding ----
            //
            // This loop used to bind ONE material (`PrimaryMaterial()`) and its maps
            // before drawing every mesh. On a 60-mesh / 15-material character that is
            // silently wrong: every panel samples the first material's atlas, which on
            // this asset is the near-white skin map - so a fully textured model
            // rendered white, and the few coloured patches that showed through were
            // the one image's non-white corners landing somewhere plausible.
            //
            // The maps and the material scalars are now chosen per mesh through
            // `meshMaterial()[i]`, and the `uHas*` flags move with them.
            const bool useMaps = view_.maps;
            const bool useNormal = useMaps && view_.normalMap;
            const auto& meshMat = model.meshMaterial();
            const auto& materials = model.materials();

            int boundMaterial = -2;   // -2 = nothing bound yet, -1 = out of range
            bool blending = glIsEnabled(GL_BLEND) == GL_TRUE;   // tracked, not assumed off
            bool cullingOff = false;                           // tracked: two-sided materials clear it
            if (meshMat.empty()) {
                // A rig with no material list at all: bind the fallbacks once so every
                // sampler still points at a complete texture (Apple drops the draw
                // otherwise) and let the flags switch the sampling off.
                const gldx::Texture2D& albedo = model.fallbackAlbedo();
                const gldx::Texture2D& normal = model.fallbackNormal();
                const gldx::Texture2D& scalar = model.fallbackScalar();
                albedo.Bind(0);
                normal.Bind(1);
                scalar.Bind(2);
                scalar.Bind(3);
                skin_->Set("uAlbedo", 0);
                skin_->Set("uNormal", 1);
                skin_->Set("uMetallic", 2);
                skin_->Set("uRoughness", 3);
                skin_->Set("uHasAlbedo", 0);
                skin_->Set("uHasNormal", 0);
                skin_->Set("uHasMetallic", 0);
                skin_->Set("uHasRoughness", 0);
                boundMaterial = -1;
            }

            for (std::size_t mi = 0; mi < model.meshes().size(); ++mi) {
                const int matIndex = (mi < meshMat.size()) ? meshMat[mi] : -1;

                if (view_.hideMaterial >= 0 && matIndex == view_.hideMaterial) continue;
                if (view_.onlyMaterial >= 0 && matIndex != view_.onlyMaterial) continue;
                // Parts the asset switched off (PMX diffuse alpha 0) stay off: the
                // author's default look is the reference, and these are optional
                // clothing, not missing data. `--on allparts` draws them anyway for
                // comparison.
                if (!view_.allParts && model.isHidden(matIndex)) continue;

                if (matIndex != boundMaterial) {
                    boundMaterial = matIndex;
                    const bool hasAlbedo = useMaps && model.hasAlbedo(matIndex);
                    const bool hasNormal = useNormal && model.hasNormal(matIndex);
                    const bool hasMetallic = useMaps && model.hasMetallic(matIndex);
                    const bool hasRoughness = useMaps && model.hasRoughness(matIndex);

                    model.AlbedoOrFallback(matIndex).Bind(0);
                    model.NormalOrFallback(matIndex).Bind(1);
                    model.MetallicOrFallback(matIndex).Bind(2);
                    model.RoughnessOrFallback(matIndex).Bind(3);
                    skin_->Set("uAlbedo", 0);
                    skin_->Set("uNormal", 1);
                    skin_->Set("uMetallic", 2);
                    skin_->Set("uRoughness", 3);
                    skin_->Set("uHasAlbedo", hasAlbedo ? 1 : 0);
                    skin_->Set("uHasNormal", hasNormal ? 1 : 0);
                    skin_->Set("uHasMetallic", hasMetallic ? 1 : 0);
                    skin_->Set("uHasRoughness", hasRoughness ? 1 : 0);

                    if (matIndex >= 0 && static_cast<std::size_t>(matIndex) < materials.size()) {
                        const gldx::SkinnedMaterial& m = materials[static_cast<std::size_t>(matIndex)];
                        if (std::getenv("GLDX_MAT"))
                            std::fprintf(stderr, "[mat] %d base=(%.2f %.2f %.2f %.2f) alb=%d\n",
                                         matIndex, (double)m.baseColor.r, (double)m.baseColor.g,
                                         (double)m.baseColor.b, (double)m.baseColor.a,
                                         m.albedoTexture);
                        skin_->Set("uBaseColor", m.baseColor);
                        skin_->Set("uMetallicFactor", m.metallic);
                        skin_->Set("uRoughnessFactor", m.roughness);
                    }
                }

                if (std::getenv("GLDX_DRAWMESH")) {
                    static int shown = 0;
                    if (shown++ < 25)
                        std::fprintf(stderr, "[draw] mesh %zu mat=%d verts=%zu\n", mi, matIndex,
                                     model.meshes()[mi].vertexCount());
                }

                // ---- alpha blending, per material ----
                //
                // MMD face models paint blush, eyeliner and the mouth line on one
                // mostly-transparent sheet laid over the head. Drawn opaque it is a
                // solid-band tattoo; drawn blended it is a soft wash. The blend state
                // is ours to set because this pass owns GL state, so it has to be set
                // per mesh, not once (one character legitimately mixes both).
                //
                // Depth writes are left OFF for blended meshes: that sheet sits a few
                // millimetres off the skin, and with depth writes on it would occlude
                // the eyes drawn later in the same frame. It is painted geometry, not
                // a solid surface, so it has no business in the depth buffer.
                const bool blended = useMaps && model.isBlended(matIndex);
                if (blended != blending) {
                    blending = blended;
                    if (blended) {
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        glDepthMask(GL_FALSE);
                    } else {
                        glDisable(GL_BLEND);
                        glDepthMask(GL_TRUE);
                    }
                }
                // The blend equation multiplies by the FRAGMENT alpha, so switching
                // GL_BLEND on is only half the job - the shader has to emit the
                // texture's alpha rather than a constant 1.0. Set on every mesh
                // because the value follows the material, not the pass.
                skin_->Set("uBlended", blended ? 1 : 0);

                // ---- culling, per material ----
                //
                // The asset's own materials say which surfaces are single-sided sheets.
                // The scalloped lace trim on this skirt's hem is one: culled, its back
                // faces vanish and the hem reads as a row of dark notches that move with
                // the cloth - which looks exactly like a skinning tear, and is not one
                // (`GLDX_NO_SECONDARY=1` leaves them there). The loader parsed that flag
                // from the start and nothing consumed it.
                //
                // Per material, not per model: this character legitimately mixes both,
                // and turning culling off globally shows the unlit inside of every solid
                // part (measured: the skirt's interior becomes a black bowl that reads far
                // worse than the notches it fixes). Two-sided sheets are flat, so they
                // have no inside to show.
                const bool twoSided = view_.twoSided && model.isTwoSided(matIndex);
                if (twoSided != cullingOff) {
                    cullingOff = twoSided;
                    if (twoSided) {
                        glDisable(GL_CULL_FACE);
                    } else {
                        glEnable(GL_CULL_FACE);
                        glCullFace(GL_BACK);
                    }
                }

                model.meshes()[mi].Draw();
                ++draws;
            }

            // Pass-local GL state is restored, never inherited: the next pass (the HUD,
            // in particular) draws its glyphs blended and assumes it is the one turning
            // blending on.
            if (blending) {
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
            }
            // Culling is pass-level state and the next pass may assume GL defaults, so
            // it is restored here rather than left wherever the last material put it.
            if (cullingOff) {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_BACK);
            }
        }

        if (view_.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);   // pass-local state is restored
        view_.drawCalls = draws;
    }

private:
    Character& character_;
    View&      view_;
    std::optional<gldx::ShaderProgram> skin_;
    std::optional<gldx::ShaderProgram> ground_;
    gldx::Mesh groundMesh_;
};

// ---------------------------------------------------------------------------
// The panel: what the demo asserts (walking, skinned) plus the evidence a
// reviewer would otherwise have to infer from the picture (bone count, palette
// fill, the bind-pose check, the phase the frame was frozen at).
// ---------------------------------------------------------------------------

}  // namespace anim
