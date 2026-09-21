module;

#include "gfx/gmf.hpp"

#include <cstdio>

#include <glm/gtc/matrix_inverse.hpp>

module gfx;

namespace gfx {

namespace {
// Open the frame's scene target. With a PostProcessChain the 3D passes render
// linear HDR into its (MSAA) offscreen buffer; without one (the minimal, opt-in
// pipeline) they render straight at the default framebuffer and clear it here,
// so no HDR/MSAA/bloom chain has to exist just to put pixels on screen.
void BeginSceneTarget(RenderFrame& f) {
    if (f.post) {
        f.post->Resize(f.fbWidth, f.fbHeight, 4);
        f.post->BeginScene();
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, f.fbWidth, f.fbHeight);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
} // namespace

void ShadowPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("ShadowPass::Execute");
    if (!f.shadow.enabled || !f.camera || !f.shadow.map || !f.shadow.depth || !f.scene) return;

    f.shadow.map->Update(*f.camera, f.shadow.sunToward);
    const ShaderProgram& depth = *f.shadow.depth;
    depth.Use();
    for (int i = 0; i < kCascadeCount; ++i) {
        f.shadow.map->BeginCascade(i);
        depth.Set("uLightMat", f.shadow.map->data().lightMat[i]);
        for (SceneNode* n : f.scene->ShadowCasters()) {
            depth.Set("uModel", n->world());
            n->mesh()->Draw();
        }
        f.shadow.map->EndCascade();
    }
    f.shadow.map->Upload();
}

void GeometryPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("GeometryPass::Execute");
    // The scene minimum: a camera, the lighting UBO, a PBR program and a scene
    // to walk. Everything else (post / shadow / IBL / sky) is optional and each
    // guarded where it is used - a pass may only be handed the subsystems the
    // application actually brought.
    if (!f.camera || !f.lights || !f.pbr || !f.scene) return;

    // Linear HDR into the (MSAA) scene target, or straight at the window when
    // no post chain was brought.
    BeginSceneTarget(f);

    f.lights->Bind();
    if (f.shadow.map) f.shadow.map->BindUniform();

    const ShaderProgram& pbr = *f.pbr;
    pbr.Use();
    pbr.Set("uViewProj", f.viewProj);
    // Ask for the feature only where the data behind it exists: the shader
    // would otherwise sample a shadow array or IBL set that was never bound.
    pbr.Set("uUseShadow", (f.shadow.enabled && f.shadow.map) ? 1 : 0);
    pbr.Set("uUseIbl", (f.ibl.enabled && f.sky.env) ? 1 : 0);

    if (f.shadow.map) f.shadow.map->Bind(texunit::shadowArray);
    if (f.sky.env) {
        unsigned unit = texunit::irradiance;
        f.sky.env->BindIrradiance(unit);
        f.sky.env->BindPrefilter(unit);
        f.sky.env->BindBrdf(unit);
    }

    int visible = 0;
    for (SceneNode* n : f.scene->Renderables()) {
        if (f.frustum && !f.frustum->SphereVisible(n->worldCenter(), n->worldRadius())) continue;
        ++visible;
        pbr.Set("uModel", n->world());
        pbr.Set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(n->world()))));
        if (n == f.selected) {
            PbrMaterial hl = *n->material();          // copy: highlight is per-frame
            hl.baseColor = glm::vec4(1.0f, 0.85f, 0.2f, 1.0f);
            hl.metallic  = 0.1f;
            hl.roughness = 0.25f;
            hl.Apply(pbr);
        } else {
            n->material()->Apply(pbr);
        }
        n->mesh()->Draw();
    }
    f.visibleCount = visible;
    f.totalNodes   = static_cast<int>(f.scene->Renderables().size());

    // GPU-instanced field, lit by the same LightingBlock UBO.
    if (f.instances.enabled && f.instances.prog && f.instances.field && f.instances.field->valid()) {
        f.instances.prog->Use();
        f.instances.prog->Set("uViewProj", f.viewProj);
        f.instances.field->Draw();
    }
}

void SkyboxPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("SkyboxPass::Execute");
    // Drawn while the scene target is still open (before PostProcessPass closes
    // it). The LEQUAL-depth trick fills only the pixels no opaque geometry
    // covered; SkyboxViewProj strips the view translation so the cube always
    // surrounds the camera, and stays a perspective box in orthographic mode.
    if (!f.camera || !f.sky.box || !f.sky.env || !f.sky.box->ready()) return;
    f.sky.box->Draw(f.camera->SkyboxViewProj(), f.sky.env->sky(), texunit::skybox);
}

namespace {

// Common setup for both voxel passes: program uniforms + the block texture
// array + the lighting UBO. The LightingBlock binding point itself is mapped
// once by the caller (GLSL 4.10 has no layout(binding=...)), so this only
// pushes the per-frame values.
void UseVoxelProgram(RenderFrame& f, VoxelPipeline& vx, float alphaCutoff,
                     float overrideAlpha, const glm::vec2& uvScroll) {
    const ShaderProgram& p = *vx.prog;
    p.Use();
    p.Set("uViewProj", f.viewProj);
    p.Set("uAtlas", static_cast<int>(texunit::voxelAtlas));
    p.Set("uAlphaCutoff", alphaCutoff);
    p.Set("uOverrideAlpha", overrideAlpha);
    p.Set("uUvScroll", uvScroll);
    vx.atlas->Bind(texunit::voxelAtlas);
    if (f.lights) f.lights->Bind();
}

} // namespace

void VoxelOpaquePass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("VoxelOpaquePass::Execute");
    if (!f.camera) return;

    // Opens the scene target exactly like GeometryPass: a voxel demo replaces
    // GeometryPass in the pipeline, so somebody has to resize + begin it — even
    // on the frames while the world is still streaming in and nothing is
    // uploadable yet (an unresized HDR target would leave the composite
    // binding an empty texture). With no post chain it renders at the window.
    BeginSceneTarget(f);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    // The mesher emits each face once with outward winding, so a closed chunk
    // culls its own backs for free. doubleSided turns that off so a camera that
    // gets inside or under terrain still sees the far walls of the cavity
    // instead of looking through culled faces into nothing.
    if (vx_ && vx_->doubleSided) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }

    if (vx_ && vx_->ready()) {
        // Cutout (leaves) resolves in the same submission as solid blocks: the
        // alpha-test discard costs nothing extra and keeps foliage in the
        // opaque depth buffer, which is what lets water sort against it.
        UseVoxelProgram(f, *vx_, vx_->leafCutoff, 1.0f, glm::vec2(0.0f));
        const ShaderProgram& p = *vx_->prog;

        int visible = 0;
        for (VoxelChunkGpu& c : *vx_->chunks) {
            if (f.frustum && !f.frustum->SphereVisible(c.center, c.radius)) continue;
            if (!c.opaque.valid() || c.opaque.indexCount() == 0) continue;
            ++visible;
            p.Set("uModel", c.model);
            c.opaque.Draw();
        }
        f.visibleCount = visible;
        f.totalNodes   = static_cast<int>(vx_->chunks->size());
    }
}

void VoxelTransparentPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("VoxelTransparentPass::Execute");
    if (!f.camera || !vx_ || !vx_->ready()) return;

    // Back-to-front by camera distance: with depth writes off, blending only
    // composites correctly when the farthest surfaces go first. Per-chunk
    // sorting is coarse (a water sheet is one draw), the classic voxel-game
    // trade-off against sorting tens of thousands of individual quads.
    const glm::vec3 eye = f.camera->Position();
    order_.clear();
    for (std::size_t i = 0; i < vx_->chunks->size(); ++i) {
        const VoxelChunkGpu& c = (*vx_->chunks)[i];
        if (f.frustum && !f.frustum->SphereVisible(c.center, c.radius)) continue;
        if (!c.hasTransparent || !c.transparent.valid()
            || c.transparent.indexCount() == 0) continue;
        order_.push_back(static_cast<int>(i));
    }
    std::sort(order_.begin(), order_.end(), [&](int a, int b) {
        // Squared distances (no glm::distance2: that is a GTX extension, the
        // engine sticks to core glm + gtc).
        const glm::vec3 da = (*vx_->chunks)[a].center - eye;
        const glm::vec3 db = (*vx_->chunks)[b].center - eye;
        return glm::dot(da, da) > glm::dot(db, db);
    });

    // Water scrolls its uv slowly; a moving texture reads as liquid far more
    // cheaply than any vertex animation could.
    const float t = vx_->time;
    UseVoxelProgram(f, *vx_, 0.0f, vx_->waterAlpha,
                    glm::vec2(t * 0.02f, t * 0.013f));
    const ShaderProgram& p = *vx_->prog;

    // Two-sided: viewed from under the surface the top faces would otherwise
    // vanish, and translucent water is exactly where that shows. Culling and
    // the depth-write state are restored below, because every later pass
    // assumes the Renderer's defaults.
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    for (int idx : order_) {
        VoxelChunkGpu& c = (*vx_->chunks)[static_cast<std::size_t>(idx)];
        p.Set("uModel", c.model);
        c.transparent.Draw();
    }

    if (f.particles && f.particles->valid()) {
        const float fovRad = glm::radians(f.camera->FovY());
        const float pixelScale =
            static_cast<float>(f.fbHeight) / (2.0f * std::tan(fovRad * 0.5f));
        f.particles->Draw(f.viewProj, eye, pixelScale, f.overlay.white);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

void PostProcessPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("PostProcessPass::Execute");
    if (!f.post) return;   // no HDR target: nothing to composite from
    f.post->EndScene();
    if (f.useBloom) {
        // The PBR spheres are lit to roughly 0.2-0.9 linear-HDR (their bright
        // tops mirror the ~0.78 sky; the sun glint is a 1-2px spot that is not
        // reliably camera-facing), so the old 1.0 cut-off keyed the bright pass
        // on almost nothing and the materials never changed when bloom toggled.
        // A 0.35 cut-off sits inside the objects' lit band, and the bloom buffer
        // is half-resolution with a wide 6-iteration blur (see PostProcessChain)
        // so each key glint spreads into a readable halo rather than a few dead
        // pixels. The dark floor and shadow side stay below the cut-off.
        f.post->SetBloomThreshold(0.35f);
        f.post->SetBloomIterations(6);
        f.post->RenderBloom();
        f.post->SetBloomStrength(2.0f);
    } else {
        f.post->SetBloomStrength(0.0f);
    }
    f.post->Composite();
}

void DebugHudPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("DebugHudPass::Execute");

    // World-space line overlay on the tone-mapped screen.
    if ((f.overlay.useDebug || f.selected) && f.overlay.debug && f.scene) {
        f.overlay.debug->Clear();
        if (f.overlay.useDebug) {
            f.overlay.debug->PushAxes(glm::vec3(0.0f), 2.0f);
            for (SceneNode* n : f.scene->Renderables()) {
                const bool sel = (n == f.selected);
                f.overlay.debug->PushBoxCenter(n->worldCenter(), glm::vec3(n->worldRadius()),
                                       sel ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f)
                                           : glm::vec4(0.2f, 0.9f, 0.4f, 0.6f));
            }
        } else if (f.selected) {
            f.overlay.debug->PushBoxCenter(f.selected->worldCenter(),
                                   glm::vec3(f.selected->worldRadius()),
                                   glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
        }
        f.overlay.debug->Draw(f.viewProj);
    }

    // 2D HUD.
    if (!f.overlay.sprite || !f.overlay.font || !f.overlay.white) return;
    constexpr std::string_view kHudControls =
        "drag=orbit scroll=zoom A/D W/S=sun  1=shd 2=ibl 3=blm 4=dbg 5=inst 6=sky"
        " Tab=proj  rclick=pick";
    // The control line below is the widest string in the block; the panel is
    // sized to it rather than a magic constant, so adding a key hint cannot make
    // the text stick out of its own background.
    const float hintW = TextRenderer::Measure(*f.overlay.font, kHudControls, 16.0f);
    const float panelW = std::max(470.0f, 12.0f + hintW + 12.0f);
    f.overlay.sprite->Begin(*f.overlay.white, f.fbWidth, f.fbHeight);
    f.overlay.sprite->Draw(*f.overlay.white, 0.0f, 0.0f, panelW, 118.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                   glm::vec4(0.0f, 0.0f, 0.0f, 0.35f));
    char line[220];
    std::snprintf(line, sizeof(line),
                  "GLFW_Template - Phase 5: scene graph + render passes   %.0f fps",
                  f.smoothedFps);
    TextRenderer::Draw(*f.overlay.sprite, *f.overlay.font, line, 12.0f, 8.0f, 22.0f, glm::vec4(1.0f));
    std::snprintf(line, sizeof(line),
                  "CPU %.2f ms   GPU %.2f ms   visible %d/%d   sel %d",
                  f.overlay.profiler ? f.overlay.profiler->CpuMs() : 0.0f,
                  f.overlay.profiler ? f.overlay.profiler->GpuMs() : 0.0f,
                  f.visibleCount, f.totalNodes, f.selected ? f.selected->id : -1);
    TextRenderer::Draw(*f.overlay.sprite, *f.overlay.font, line, 12.0f, 34.0f, 20.0f,
                       glm::vec4(0.75f, 0.85f, 1.0f, 1.0f));
    std::snprintf(line, sizeof(line),
                  "Shadow %s  IBL %s  Bloom %s  Debug %s  Inst %s  Sky %s  %s",
                  f.shadow.enabled ? "ON" : "OFF", f.ibl.enabled ? "ON" : "OFF",
                  f.useBloom ? "ON" : "OFF", f.overlay.useDebug ? "ON" : "OFF",
                  f.instances.enabled ? "ON" : "OFF", f.sky.box ? "ON" : "OFF",
                  f.ortho ? "ORTHO" : "PERSP");
    TextRenderer::Draw(*f.overlay.sprite, *f.overlay.font, line, 12.0f, 60.0f, 20.0f,
                       glm::vec4(0.75f, 0.85f, 1.0f, 1.0f));
    TextRenderer::Draw(*f.overlay.sprite, *f.overlay.font, kHudControls,
                       12.0f, 86.0f, 16.0f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
    f.overlay.sprite->End();

    if (f.overlay.profiler) f.overlay.profiler->EndFrame();
}

} // namespace gfx
