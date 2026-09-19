#include "gfx/render/PostProcessChain.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "gfx/core/RenderContext.h"
#include "gfx/shader/PostProcessShaders.h"

namespace gfx {

bool PostProcessChain::Init() {
    RenderContext::AssertRenderThread("PostProcessChain::Init");

    auto compile = [](ShaderProgram& slot, const char* frag, const char* name) -> bool {
        auto p = ShaderProgram::CreateFromSource(shaders::kPostVertex, frag);
        if (!p) {
            std::fprintf(stderr, "[PostProcessChain] %s shader failed: %s\n", name, p.error().c_str());
            return false;
        }
        slot = std::move(*p);
        return true;
    };

    if (!compile(bright_,     shaders::kBrightPassFragment, "bright"))    return false;
    if (!compile(blur_,       shaders::kBlurFragment,       "blur"))      return false;
    if (!compile(composite_,  shaders::kCompositeFragment,  "composite")) return false;

    emptyVao_.Create();

    GLint maxSamples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);   // shared limit for MSAA textures
    maxSamples_ = maxSamples > 0 ? static_cast<int>(maxSamples) : 1;
    return true;
}

void PostProcessChain::Resize(int width, int height, int msaaSamples) {
    RenderContext::AssertRenderThread("PostProcessChain::Resize");
    if (width <= 0 || height <= 0) return;
    if (width == width_ && height == height_ && msaaSamples == msaaSamples_ && hdrColor_.valid())
        return;   // no change

    width_ = width;
    height_ = height;
    bloomW_ = std::max(1, width / 2);
    bloomH_ = std::max(1, height / 2);
    msaaSamples_ = std::clamp(msaaSamples, 1, maxSamples_);

    hdrColor_.Allocate(RenderTexture::Format::Rgba16F, width_, height_, 1, false);
    bloomA_.Allocate(RenderTexture::Format::Rgba16F, bloomW_, bloomH_, 1, false);
    bloomB_.Allocate(RenderTexture::Format::Rgba16F, bloomW_, bloomH_, 1, false);

    if (msaaSamples_ > 1) {
        msaaColor_.AllocateMultisample(RenderTexture::Format::Rgba16F, width_, height_, msaaSamples_);
        msaaDepth_.AllocateMultisample(RenderTexture::Format::Depth24, width_, height_, msaaSamples_);
    }

    // Attachments (Create() is idempotent, so these survive repeated resizes).
    hdrFbo_.Create();
    hdrFbo_.AttachColor(hdrColor_, 0, 0);
    bloomFboA_.Create();
    bloomFboA_.AttachColor(bloomA_, 0, 0);
    bloomFboB_.Create();
    bloomFboB_.AttachColor(bloomB_, 0, 0);
    if (msaaSamples_ > 1) {
        msaaFbo_.Create();
        msaaFbo_.AttachColorMultisample(msaaColor_, 0);
        msaaFbo_.AttachDepthMultisample(msaaDepth_);
    }

    // Defensive completeness check: an attachment mistake (e.g. a mismatched
    // multisample count between colour and depth) would otherwise only surface
    // as a black frame with no diagnostic.
    auto check = [](Framebuffer& fbo, const char* name) {
        fbo.Bind();
        if (const GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            st != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "[PostProcessChain] %s incomplete: 0x%x\n", name, st);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    };
    check(hdrFbo_, "hdr");
    check(bloomFboA_, "bloomA");
    check(bloomFboB_, "bloomB");
    if (msaaSamples_ > 1) check(msaaFbo_, "msaa");
}

void PostProcessChain::BeginScene() {
    RenderContext::AssertRenderThread("PostProcessChain::BeginScene");
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    if (msaaSamples_ > 1) msaaFbo_.Bind();
    else hdrFbo_.Bind();
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PostProcessChain::EndScene() {
    RenderContext::AssertRenderThread("PostProcessChain::EndScene");
    if (msaaSamples_ > 1) {
        Framebuffer::ResolveColorTo(msaaFbo_, hdrFbo_, width_, height_);
    }
}

void PostProcessChain::DrawFullscreen() {
    emptyVao_.Bind();
    glDrawArrays(GL_TRIANGLES, 0, 3);
    emptyVao_.Unbind();
}

void PostProcessChain::RenderBloom() {
    RenderContext::AssertRenderThread("PostProcessChain::RenderBloom");
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // Bright pass: HDR resolve -> bloomA (half res).
    bloomFboA_.Bind();
    glViewport(0, 0, bloomW_, bloomH_);
    bright_.Use();
    hdrColor_.Bind(0);
    bright_.Set("uImage", 0);
    bright_.Set("uThreshold", bloomThreshold_);
    bright_.Set("uSoftKnee", bloomSoftKnee_);
    DrawFullscreen();

    // Separable gaussian blur, ping-ponged bloomA <-> bloomB. Final result ends
    // up back in bloomA after each full H+V pair.
    const float spread = 1.0f;
    blur_.Use();
    blur_.Set("uImage", 0);
    for (int i = 0; i < bloomIterations_; ++i) {
        bloomFboB_.Bind();
        bloomA_.Bind(0);
        blur_.Set("uDirection", glm::vec2(spread / static_cast<float>(bloomW_), 0.0f));
        DrawFullscreen();

        bloomFboA_.Bind();
        bloomB_.Bind(0);
        blur_.Set("uDirection", glm::vec2(0.0f, spread / static_cast<float>(bloomH_)));
        DrawFullscreen();
    }

    glEnable(GL_CULL_FACE);
}

void PostProcessChain::Composite() {
    RenderContext::AssertRenderThread("PostProcessChain::Composite");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    composite_.Use();
    hdrColor_.Bind(0);
    bloomA_.Bind(1);
    composite_.Set("uScene", 0);
    composite_.Set("uBloom", 1);
    composite_.Set("uBloomStrength", bloomStrength_);
    composite_.Set("uExposure", exposure_);
    DrawFullscreen();

    ShaderProgram::Unuse();
}

} // namespace gfx
