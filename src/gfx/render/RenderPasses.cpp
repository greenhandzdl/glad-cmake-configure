module;

#include "gfx/gmf.hpp"

#include <cstdio>

#include <glm/gtc/matrix_inverse.hpp>

module gfx;

namespace gfx {

void ShadowPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("ShadowPass::Execute");
    if (!f.useShadow || !f.shadow || !f.depth || !f.scene) return;

    f.shadow->Update(*f.camera, f.sunToward);
    const ShaderProgram& depth = *f.depth;
    depth.Use();
    for (int i = 0; i < kCascadeCount; ++i) {
        f.shadow->BeginCascade(i);
        depth.Set("uLightMat", f.shadow->data().lightMat[i]);
        for (SceneNode* n : f.scene->ShadowCasters()) {
            depth.Set("uModel", n->world());
            n->mesh()->Draw();
        }
        f.shadow->EndCascade();
    }
    f.shadow->Upload();
}

void GeometryPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("GeometryPass::Execute");

    // Linear HDR into the (MSAA) scene target.
    f.post->Resize(f.fbWidth, f.fbHeight, 4);
    f.post->BeginScene();

    f.lights->Bind();
    f.shadow->BindUniform();

    const ShaderProgram& pbr = *f.pbr;
    pbr.Use();
    pbr.Set("uViewProj", f.viewProj);
    pbr.Set("uUseShadow", f.useShadow ? 1 : 0);
    pbr.Set("uUseIbl", f.useIbl ? 1 : 0);

    f.shadow->Bind(texunit::shadowArray);
    unsigned unit = texunit::irradiance;
    f.env->BindIrradiance(unit);
    f.env->BindPrefilter(unit);
    f.env->BindBrdf(unit);

    int visible = 0;
    for (SceneNode* n : f.scene->Renderables()) {
        if (!f.frustum->SphereVisible(n->worldCenter(), n->worldRadius())) continue;
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
    if (f.useInstances && f.instProg && f.instField && f.instField->valid()) {
        f.instProg->Use();
        f.instProg->Set("uViewProj", f.viewProj);
        f.instField->Draw();
    }

    // Skybox last (LEQUAL-depth trick fills uncovered pixels). SkyboxViewProj
    // strips the view translation (cube surrounds the camera) and always uses a
    // perspective box, so it stays correct in orthographic mode too.
    f.skybox->Draw(f.camera->SkyboxViewProj(), f.env->sky(), texunit::skybox);
}

void PostProcessPass::Execute(RenderFrame& f) {
    RenderContext::AssertRenderThread("PostProcessPass::Execute");
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
    if ((f.useDebug || f.selected) && f.debug) {
        f.debug->Clear();
        if (f.useDebug) {
            f.debug->PushAxes(glm::vec3(0.0f), 2.0f);
            for (SceneNode* n : f.scene->Renderables()) {
                const bool sel = (n == f.selected);
                f.debug->PushBoxCenter(n->worldCenter(), glm::vec3(n->worldRadius()),
                                       sel ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f)
                                           : glm::vec4(0.2f, 0.9f, 0.4f, 0.6f));
            }
        } else if (f.selected) {
            f.debug->PushBoxCenter(f.selected->worldCenter(),
                                   glm::vec3(f.selected->worldRadius()),
                                   glm::vec4(1.0f, 0.85f, 0.2f, 1.0f));
        }
        f.debug->Draw(f.viewProj);
    }

    // 2D HUD.
    if (!f.sprite || !f.font || !f.white) return;
    f.sprite->Begin(*f.white, f.fbWidth, f.fbHeight);
    f.sprite->Draw(*f.white, 0.0f, 0.0f, 470.0f, 118.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                   glm::vec4(0.0f, 0.0f, 0.0f, 0.35f));
    char line[220];
    std::snprintf(line, sizeof(line),
                  "GLFW_Template - Phase 5: scene graph + render passes   %.0f fps",
                  f.smoothedFps);
    TextRenderer::Draw(*f.sprite, *f.font, line, 12.0f, 8.0f, 22.0f, glm::vec4(1.0f));
    std::snprintf(line, sizeof(line),
                  "CPU %.2f ms   GPU %.2f ms   visible %d/%d   sel %d",
                  f.profiler ? f.profiler->CpuMs() : 0.0f,
                  f.profiler ? f.profiler->GpuMs() : 0.0f,
                  f.visibleCount, f.totalNodes, f.selected ? f.selected->id : -1);
    TextRenderer::Draw(*f.sprite, *f.font, line, 12.0f, 34.0f, 20.0f,
                       glm::vec4(0.75f, 0.85f, 1.0f, 1.0f));
    std::snprintf(line, sizeof(line),
                  "Shadow %s  IBL %s  Bloom %s  Debug %s  Inst %s  %s",
                  f.useShadow ? "ON" : "OFF", f.useIbl ? "ON" : "OFF",
                  f.useBloom ? "ON" : "OFF", f.useDebug ? "ON" : "OFF",
                  f.useInstances ? "ON" : "OFF",
                  f.ortho ? "ORTHO" : "PERSP");
    TextRenderer::Draw(*f.sprite, *f.font, line, 12.0f, 60.0f, 20.0f,
                       glm::vec4(0.75f, 0.85f, 1.0f, 1.0f));
    TextRenderer::Draw(*f.sprite, *f.font,
                       "drag=orbit scroll=zoom A/D W/S=sun  1=shd 2=ibl 3=blm 4=dbg 5=inst Tab=proj  rclick=pick",
                       12.0f, 86.0f, 16.0f, glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
    f.sprite->End();

    if (f.profiler) f.profiler->EndFrame();
}

} // namespace gfx
