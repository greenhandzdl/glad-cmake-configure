module;

#include "gldx/gmf.hpp"

#include <cstdio>

module gldx;

namespace gldx {

namespace {

int MipCount(int baseSize) {
    return static_cast<int>(std::floor(std::log2(static_cast<float>(baseSize)))) + 1;
}

} // namespace

bool EnvironmentMap::Generate(const glm::vec3& sunDir, int skySize, int irradianceSize, int prefilterSize) {
    RenderContext::AssertRenderThread("EnvironmentMap::Generate");

    sunDir_ = glm::length(sunDir) > 0.0f ? glm::normalize(sunDir) : glm::vec3(0.0f, -1.0f, 0.0f);
    skySize_ = skySize > 1 ? skySize : 256;
    irradianceSize_ = irradianceSize > 1 ? irradianceSize : 32;
    prefilterSize_ = prefilterSize > 1 ? prefilterSize : 256;
    prefilterMips_ = MipCount(prefilterSize_);

    if (!Compile()) return false;

    emptyVao_.Create();
    fbo_.Create();

    // HDR render targets. GL_RGB16F keeps the source cube lean; the BRDF LUT
    // is a 2D RG pair stored in an RGBA16F texture.
    sky_.AllocateCube(GL_RGB16F, skySize_, 1, GL_RGB, GL_HALF_FLOAT);
    irradiance_.AllocateCube(GL_RGB16F, irradianceSize_, 1, GL_RGB, GL_HALF_FLOAT);
    prefilter_.AllocateCube(GL_RGB16F, prefilterSize_, prefilterMips_, GL_RGB, GL_HALF_FLOAT);
    brdf_.Allocate(RenderTexture::Format::Rgba16F, 512, 512, 1, false);

    // The environment passes need no depth buffer and no culling.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    RenderSky();
    RenderIrradiance();
    RenderPrefilter();
    RenderBrdf();

    fbo_.Unbind();
    ShaderProgram::Unuse();
    glClearDepth(1.0f);
    // Restore both states disabled above (depth was restored here already, but
    // culling was not, leaking a cull-off state into every later pass).
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    if (!valid()) {
        std::fprintf(stderr, "[EnvironmentMap] generation produced invalid textures\n");
        return false;
    }
    return true;
}

bool EnvironmentMap::Compile() {
    auto attach = [](ShaderProgram& slot, std::expected<ShaderProgram, std::string> result,
                     const char* name) -> bool {
        if (!result) {
            std::fprintf(stderr, "[EnvironmentMap] %s shader failed: %s\n", name, result.error().c_str());
            return false;
        }
        slot = std::move(*result);
        return true;
    };

    using namespace shaders;
    if (!attach(skyGenShader_,     ShaderProgram::CreateFromSource(kIblVertex, kSkyGenFragment),      "skyGen"))      return false;
    if (!attach(irradianceShader_, ShaderProgram::CreateFromSource(kIblVertex, kIrradianceFragment),  "irradiance"))  return false;
    if (!attach(prefilterShader_,  ShaderProgram::CreateFromSource(kIblVertex, kPrefilterFragment),   "prefilter"))   return false;
    if (!attach(brdfShader_,       ShaderProgram::CreateFromSource(kIblVertex, kBrdfFragment),        "brdf"))        return false;
    return true;
}

// Every face is written through a fullscreen triangle whose shader maps its quad
// coordinate c in [-1,1] to normalize(c.x*right + c.y*up + facing), so the frame
// handed to that shader also has to be the frame a samplerCube read uses. Both come
// from gldx::CubeFaceAxis - the engine's one cube convention, documented in
// TextureCubeMap.h.
void EnvironmentMap::RenderSky() {
    skyGenShader_.Use();
    skyGenShader_.Set("uSunDir", sunDir_);
    emptyVao_.Bind();
    for (int f = 0; f < 6; ++f) {
        fbo_.AttachCubeFaceColor(sky_, kCubeFaceTargets[f], 0, 0);
        fbo_.Bind();
        fbo_.Viewport(skySize_, skySize_);
        skyGenShader_.Set("uRight",   CubeFaceAxis(f, 0));
        skyGenShader_.Set("uUp",      CubeFaceAxis(f, 1));
        skyGenShader_.Set("uFacing",  CubeFaceAxis(f, 2));
        emptyVao_.DrawArrays(GL_TRIANGLES, 0, 3);
    }
}

void EnvironmentMap::RenderIrradiance() {
    irradianceShader_.Use();
    irradianceShader_.Set("uEnv", 0);
    sky_.Bind(0);
    emptyVao_.Bind();
    for (int f = 0; f < 6; ++f) {
        fbo_.AttachCubeFaceColor(irradiance_, kCubeFaceTargets[f], 0, 0);
        fbo_.Bind();
        fbo_.Viewport(irradianceSize_, irradianceSize_);
        irradianceShader_.Set("uRight",  CubeFaceAxis(f, 0));
        irradianceShader_.Set("uUp",     CubeFaceAxis(f, 1));
        irradianceShader_.Set("uFacing", CubeFaceAxis(f, 2));
        emptyVao_.DrawArrays(GL_TRIANGLES, 0, 3);
    }
}

void EnvironmentMap::RenderPrefilter() {
    prefilterShader_.Use();
    prefilterShader_.Set("uEnv", 0);
    sky_.Bind(0);
    emptyVao_.Bind();
    const int lastLevel = prefilterMips_ - 1;
    for (int level = 0; level <= lastLevel; ++level) {
        const float roughness = lastLevel > 0 ? static_cast<float>(level) / static_cast<float>(lastLevel) : 0.0f;
        prefilterShader_.Set("uRoughness", roughness);
        const int size = (prefilterSize_ >> level) > 0 ? (prefilterSize_ >> level) : 1;
        for (int f = 0; f < 6; ++f) {
            fbo_.AttachCubeFaceColor(prefilter_, kCubeFaceTargets[f], level, 0);
            fbo_.Bind();
            fbo_.Viewport(size, size);
            prefilterShader_.Set("uRight",  CubeFaceAxis(f, 0));
            prefilterShader_.Set("uUp",     CubeFaceAxis(f, 1));
            prefilterShader_.Set("uFacing", CubeFaceAxis(f, 2));
            emptyVao_.DrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
}

void EnvironmentMap::RenderBrdf() {
    brdfShader_.Use();
    fbo_.AttachColor(brdf_, 0, 0);
    fbo_.Bind();
    fbo_.Viewport(512, 512);
    emptyVao_.Bind();
    emptyVao_.DrawArrays(GL_TRIANGLES, 0, 3);
}

void EnvironmentMap::BindIrradiance(unsigned& unit) const {
    irradiance_.Bind(unit);
    ++unit;
}

void EnvironmentMap::BindPrefilter(unsigned& unit) const {
    prefilter_.Bind(unit);
    ++unit;
}

void EnvironmentMap::BindBrdf(unsigned& unit) const {
    brdf_.Bind(unit);
    ++unit;
}

void EnvironmentMap::BindSky(unsigned& unit) const {
    sky_.Bind(unit);
    ++unit;
}

} // namespace gldx
