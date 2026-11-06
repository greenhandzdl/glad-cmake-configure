// night_forest demo — the frame: six-face FBO environment capture plus the world
// draw, both out of one program (see Passes.h).
#include "gldx/core/Platform.h"

import gldx;

#include "Passes.h"
#include "Shaders.h"
#include "Sky.h"

#include <cmath>
#include <cstdio>

#include <glm/gtc/matrix_transform.hpp>

namespace night_forest {
namespace {

// The two sampler units this program declares. Unit 0 is the engine's albedo
// slot; unit 11 is one past the engine's table (which stops at `particle` = 10),
// chosen so that no engine pass can hand it a texture of the wrong target by
// accident. A cube must never go on unit 0 and a 2D must never go on unit 11:
// Apple's GL keeps a per-target binding slot per unit, and a unit that has held
// both is exactly the case where a sampler resolves to an incomplete image.
constexpr unsigned kUnitAlbedo = gldx::texunit::albedo;
constexpr unsigned kUnitEnv    = 11u;

// The capture frustum. `near` is below the crate's own half-diagonal so the face
// it is standing on is not clipped into the reflection, and `far` is set past the
// tree line on purpose: fog finishes the distance long before the frustum does, so
// an object beyond `far` would be indistinguishable from an object that faded into
// mist - and a reflection that cuts off at a hard silhouette edge reads as a bug.
constexpr float kCaptureNear = 0.35f;
constexpr float kCaptureFar  = 260.0f;

} // namespace

bool NightPass::Init() {
    gldx::RenderContext::AssertRenderThread("NightPass::Init");
    auto made = gldx::ShaderProgram::CreateFromSources({
        {gldx::ShaderStage::Vertex,   shaders::kWorldVertex},
        {gldx::ShaderStage::Fragment, shaders::kWorldFragment},
    });
    if (!made) {
        std::fprintf(stderr, "night_forest shader error:\n%s\n", made.error().c_str());
        return false;
    }
    prog_ = std::move(*made);
    // GLSL 4.10 forbids layout(binding=...) on uniform blocks, so the lighting
    // UBO is mapped here rather than in the source - and it must carry the engine's
    // binding number, because LightBuffer::Bind() is what makes that index point at
    // the buffer main.cpp updates every frame.
    prog_.SetBlockBinding("LightingBlock", gldx::LightBuffer::kBinding);
    // Sampler-unit uniforms are program state, not draw state: pointed once here,
    // and what varies per draw is which texture those units hold.
    prog_.Set("uAlbedo", static_cast<int>(kUnitAlbedo));
    prog_.Set("uEnvCube", static_cast<int>(kUnitEnv));
    return true;
}

void NightPass::EnsureCaptureTargets(int size) {
    gldx::RenderContext::AssertRenderThread("NightPass::EnsureCaptureTargets");
    if (size < 32) size = 32;      // a 16px cube with mips is a blur, not a reflection
    if (envSizeBuilt_ == size && capture_.valid() && captureFbo_.valid()
        && captureDepth_.valid()) return;

    // RGB16F rather than RGBA8: the captured world is linear HDR and the lanterns
    // inside it run above 1.0. Stored at 8 bits they clamp to white and the
    // reflection loses the only bright things it has to show.
    const int levels = static_cast<int>(std::floor(std::log2(static_cast<float>(size)))) + 1;
    capture_.AllocateCube(GL_RGB16F, size, levels, GL_RGBA, GL_HALF_FLOAT);
    captureDepth_.Allocate(gldx::RenderTexture::Format::Depth24, size, size);
    if (!captureFbo_.valid()) captureFbo_.Create();

    // Only the depth attachment is set up once. Colour is re-attached per face
    // immediately before each draw (see CaptureEnvironment): attaching all six to
    // slot 0 up front would leave just the last one bound.
    captureFbo_.AttachDepth(captureDepth_);
    captureFailed_ = !captureFbo_.CheckComplete();
    envSizeBuilt_  = captureFailed_ ? 0 : size;
    capturedOnce_  = false;
    lastCenter_    = glm::vec3(0.0f, 1e9f, 0.0f);   // force a fresh capture
    if (captureFailed_)
        std::fprintf(stderr, "night_forest: %d px capture FBO incomplete,"
                     " the crate falls back to the star cube\n", size);
}

void NightPass::CaptureEnvironment(gldx::RenderFrame& f, const glm::vec3& center) {
    gldx::RenderContext::AssertRenderThread("NightPass::CaptureEnvironment");
    EnsureCaptureTargets(settings.envSize);
    if (captureFailed_) return;

    const glm::mat4 proj = glm::perspective(
        glm::radians(90.0f), 1.0f, kCaptureNear, kCaptureFar);
    facesLastFrame_ = 0;

    for (int face = 0; face < 6; ++face) {
        const glm::vec3 up  = CubeFaceAxis(face, 1);
        const glm::vec3 fwd = CubeFaceAxis(face, 2);
        // A right-handed camera looking along `facing` with `up` has screen-right
        // cross(facing, up), which is the axis gldx::CubeFaceAxis lists as element 0 -
        // so the view built here and the texel direction built in Sky.cpp are the same
        // frame. That is the entire reason the table was measured: paint a moon at
        // one direction, capture along six faces, and the moon must reappear on the
        // face whose `facing` points at it.
        const glm::mat4 view = glm::lookAt(center, center + fwd, up);

        // The UBO carries the eye position, and the eye of this face is the crate's
        // centre - not the player's. Fog is computed from cameraPos, so a face that
        // left the player's eye in place would bake the player's fog distance into
        // every reflection in the scene, and the specular's V would be lit from a
        // direction the camera it was captured with never had.
        if (f.lights) f.lights->Update(f.lightSetup, center);

        captureFbo_.AttachCubeFaceColor(capture_, gldx::kCubeFaceTargets[face], 0, 0);
        // AttachCubeFaceColor binds, attaches and then unbinds (Framebuffer.cpp),
        // so this Bind() is load-bearing rather than tidy: without it the draw
        // would land on whichever target was last bound, and the cube would stay
        // empty while the window silently grew a second forest.
        captureFbo_.Bind();
        captureFbo_.Viewport(envSizeBuilt_, envSizeBuilt_);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // Translation-stripped for the sky: a cube map face is a view of the
        // horizon from this point, and the sky must not sit 16 metres closer on one
        // side of the box than the other.
        DrawWorld(proj * view, proj * glm::mat4(glm::mat3(view)), false);
        ++facesLastFrame_;
    }

    // The reflection is sampled at a mip level above zero (uEnvLod), so the chain
    // has to exist after the pixels are in it - and it has to be regenerated every
    // capture, because glTexStorage2D allocated the levels but only level 0 was
    // ever written.
    capture_.GenerateMipmaps();
    captureFbo_.Unbind();
    capturedOnce_ = true;

    // Hand the lighting block back to the eye the rest of the frame expects.
    if (f.lights)
        f.lights->Update(f.lightSetup, f.camera ? f.camera->Position() : glm::vec3(0.0f));
}

void NightPass::DrawWorld(const glm::mat4& viewProj, const glm::mat4& skyViewProj,
                          bool includeCrate) {
    gldx::RenderContext::AssertRenderThread("NightPass::DrawWorld");
    const Scene* s = settings.scene;
    if (!s || !prog_.valid()) return;

    // Opaque defaults. Re-asserted on every call rather than once per frame: this
    // function runs seven times a frame (six faces plus the window) and the skybox
    // renderer in the middle of it changes depth function and cull state.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glFrontFace(GL_CCW);
    if (settings.cull) { glEnable(GL_CULL_FACE); glCullFace(GL_BACK); }
    else glDisable(GL_CULL_FACE);

    prog_.Use();
    prog_.Set("uViewProj", viewProj);

    // ---- which cube unit 11 is about to hold --------------------------------
    // The crate is the only thing that ever *samples* unit 11, so the phase that
    // does not draw the crate does not bind the cube it is writing. During a
    // capture the attachment and the sampler would otherwise be the same image, and
    // GL's feedback rules make that undefined rather than merely unwise - the
    // stars are a perfectly good thing to bind while the world is being painted.
    const bool useCapture = includeCrate && capturedOnce_ && capture_.valid();
    const gldx::TextureCubeMap* env = useCapture ? &capture_ : settings.sky;
    if (env && env->valid()) env->Bind(kUnitEnv);
    else env = nullptr;
    if (includeCrate) liveCube_ = env ? (useCapture ? kCaptured : kStar) : kNone;

    // A mirror is only allowed where there is something to mirror.
    const float envMix = env ? settings.envMix : 0.0f;

    // ---- the background, before anything can occlude it ---------------------
    // Drawn at depth 1.0 with LEQUAL, so it costs the pixels nothing that geometry
    // will cover and needs no clear colour of its own. Polygon mode is left at
    // FILL until after this: wireframing the skybox would draw the cube's own
    // triangles over the stars and turn the one thing the scene is silhouetted
    // against into a mesh diagram.
    if (settings.skybox && env)
        settings.skybox->Draw(skyViewProj, *settings.sky, gldx::texunit::skybox);
    // SkyboxRenderer::Draw leaves its own program current, so the world program has
    // to be re-selected (prog_.Use() above runs before it).
    prog_.Use();
    prog_.Set("uViewProj", viewProj);
    glPolygonMode(GL_FRONT_AND_BACK, settings.wireframe ? GL_LINE : GL_FILL);

    // One object, one draw. Everything here is an InstancedMesh, so "the forest"
    // and "the single crate" are the same call with a different instance count -
    // which is the demonstration, and why the ground is an instance too rather
    // than a special case.
    const auto draw = [&](const gldx::InstancedMesh& m, const gldx::Texture2D& tex,
                          float uvScale, float useTex, float mix, float emissive) {
        if (!m.valid()) return;
        prog_.Set("uUseTex", useTex);
        prog_.Set("uUvScale", glm::vec2(uvScale));
        prog_.Set("uEnvMix", mix);
        prog_.Set("uEnvLod", useCapture ? settings.envLod : 0.0f);
        prog_.Set("uEmissive", emissive);
        // Always a real texture, even when uUseTex is 0: an active sampler on an
        // unbound unit fails the whole call on this driver, and the flat-shaded
        // trees are the majority of the draws here.
        tex.Bind(kUnitAlbedo);
        m.Draw();
        ++tallyDraws_;
        tallyInst_ += m.count();
    };

    // The ground's tiling comes from the scene: 48 repeats across the sheet when
    // it is the engine's single quad, or tiles-per-cell when it is a height field
    // whose uv is the manual's integer lattice (Scene::groundUvScale).
    draw(s->ground(), s->grass(), s->groundUvScale(), 1.0f, 0.0f, 0.0f);
    draw(s->fir(),   s->white(), 1.0f, 0.0f, 0.0f, 0.0f);
    draw(s->broadleaf(), s->white(), 1.0f, 0.0f, 0.0f, 0.0f);
    // The lanterns carry the scene's only emissive surface, gated per-vertex so the
    // post around the glass stays iron rather than glowing like the flame.
    draw(s->lantern(), s->white(), 1.0f, 0.0f, 0.0f, settings.emissive);
    if (includeCrate) draw(s->crate(), s->wood(), 1.0f, 1.0f, envMix, 0.0f);
}

void NightPass::OpenSceneTarget(gldx::RenderFrame& f) {
    gldx::RenderContext::AssertRenderThread("NightPass::OpenSceneTarget");
    // The engine's own BeginSceneTarget is module-internal, so a demo that replaces
    // GeometryPass opens the chain itself: linear HDR into the MSAA target, and
    // PostProcessPass (next in the list) closes and composites it. Without a chain
    // the same draws go straight at the window, un-tone-mapped.
    if (f.post) {
        f.post->Resize(f.fbWidth, f.fbHeight, 4);
        f.post->BeginScene();
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(0.002f, 0.004f, 0.010f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

void NightPass::Execute(gldx::RenderFrame& f) {
    gldx::RenderContext::AssertRenderThread("NightPass::Execute");
    const Scene* s = settings.scene;
    if (!s || !prog_.valid() || f.fbWidth <= 0 || f.fbHeight <= 0) return;

    // Frame-wide Gooch parameters. These are the same for the capture and for the
    // window - a reflection shaded with a different palette than the scene it came
    // from is the seam that gives a cubemap away.
    prog_.Use();
    prog_.Set("uLightTint", settings.lightTint);
    prog_.Set("uShadowTint", settings.shadowTint);
    prog_.Set("uShadowLift", settings.shadowLift);
    prog_.Set("uBandWidth", settings.bandWidth);
    prog_.Set("uSpecPower", settings.specPower);
    prog_.Set("uSpecStrength", settings.specStrength);
    prog_.Set("uEta", 1.0f / settings.ior);
    prog_.Set("uRefract", settings.refract);

    // The lighting block has to be *bound to the index* SetBlockBinding mapped it
    // to, and LightBuffer::Update does not do that - it only re-uploads the bytes.
    // The engine's own passes call Bind() for you (RenderPasses.cpp); a demo that
    // replaces them has to. Skipping it leaves binding point 1 with no buffer, the
    // block reads zeros, and every lit surface comes out exactly black while the
    // skybox - which never touches the block - looks perfectly fine, which is the
    // part that makes this failure so expensive to diagnose.
    if (f.lights) f.lights->Bind();

    const glm::vec3 center = s->cratePose().pos;
    facesLastFrame_ = 0;
    captureDrawsLastFrame_ = 0;
    tallyDraws_ = 0;
    tallyInst_ = 0;

    // ---- job 1: the crate's environment -------------------------------------
    // Only when the box actually moved. A stationary crate's reflections are
    // stationary, and six full scene renders per frame for a box that is sitting
    // still is the kind of cost a demo should not pay for nothing - which is why
    // the HUD prints faces=0 rather than faces=6 while the crate is parked.
    const bool moved = glm::length(center - lastCenter_) > 1e-4f;
    if (settings.capture && (moved || !capturedOnce_)) CaptureEnvironment(f, center);
    if (settings.capture && !captureFailed_) lastCenter_ = center;
    if (!settings.capture) capturedOnce_ = false;   // the fallback must be honest
    captureDrawsLastFrame_ = tallyDraws_;

    // ---- job 2: the frame the window sees -----------------------------------
    OpenSceneTarget(f);
    const glm::vec3 eye = f.camera ? f.camera->Position() : glm::vec3(0.0f);
    if (f.lights) f.lights->Update(f.lightSetup, eye);   // undo a face's eye, if any
    tallyDraws_ = 0;
    tallyInst_ = 0;
    DrawWorld(f.viewProj,
              f.camera ? f.camera->SkyboxViewProj() : f.viewProj,
              true);
    drawsLastFrame_ = tallyDraws_;
    instLastFrame_  = tallyInst_;

    // ---- hand the pipeline back in the state Renderer::Init() left it --------
    // PostProcessPass and the HUD's SpriteBatch both assume the defaults, so this
    // restore is part of the pass's contract. Depth mask and blend are the two that
    // a scene pass is actually able to leave wrong.
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    f.visibleCount = drawsLastFrame_;
    f.totalNodes   = static_cast<int>(instLastFrame_);
}

} // namespace night_forest
