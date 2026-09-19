module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

namespace {

// World-space corner rays of the camera frustum at the given near/far planes.
// Returns 8 corners (x in {0,1}, y in {0,1}, z in {near,far}) unprojected to
// world space; used to fit a bounding sphere per cascade.
struct FrustumCorners { glm::vec3 nearPts[4]; glm::vec3 farPts[4]; };

FrustumCorners ComputeFrustumCorners(const Camera& cam) {
    const glm::mat4 invVP = cam.InverseViewProjection();
    const glm::vec3 eye = cam.Position();
    FrustumCorners c;
    int idx = 0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const float nx = x == 0 ? -1.0f : 1.0f;
            const float ny = y == 0 ? -1.0f : 1.0f;
            // near plane (z = -1) and far plane (z = +1) in NDC
            glm::vec4 pn = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
            glm::vec4 pf = invVP * glm::vec4(nx, ny,  1.0f, 1.0f);
            c.nearPts[idx] = glm::vec3(pn) / pn.w;
            c.farPts[idx]  = glm::vec3(pf) / pf.w;
            ++idx;
        }
    }
    (void)eye;
    return c;
}

} // namespace

void CascadedShadowMap::Init(int resolution) {
    RenderContext::AssertRenderThread("CascadedShadowMap::Init");
    resolution_ = resolution;

    depthArray_.Allocate(RenderTexture::Format::Depth32F, resolution_, resolution_, kCascadeCount, false);
    fbo_.Create();

    Sampler::Desc sd;
    sd.minFilter = GL_LINEAR;
    sd.magFilter = GL_LINEAR;
    sd.wrapS = GL_CLAMP_TO_EDGE;
    sd.wrapT = GL_CLAMP_TO_EDGE;
    sd.compareMode = true;
    sd.compareFunc = GL_LEQUAL;
    sampler_.Create(sd);

    shadowUbo_.Create(static_cast<GLsizeiptr>(sizeof(ShadowBlockGpu)), GL_DYNAMIC_DRAW);

    data_ = ShadowBlockGpu{};
    data_.params = glm::vec4(static_cast<float>(kCascadeCount), 0.0f, 0.0f, 0.0f);
}

void CascadedShadowMap::Update(const Camera& camera, const glm::vec3& sunDirection) {
    RenderContext::AssertRenderThread("CascadedShadowMap::Update");

    const float nearZ = camera.NearPlane();
    const float farZ  = camera.FarPlane();
    const FrustumCorners corners = ComputeFrustumCorners(camera);

    // Log-uniform (practical) split distances, blended with linear for the last.
    float splitFrac[kCascadeCount + 1];
    for (int i = 0; i <= kCascadeCount; ++i) {
        const float l = static_cast<float>(i) / kCascadeCount;
        const float logPart = nearZ * std::pow(farZ / nearZ, l);
        const float linPart = nearZ * (1.0f - l) + farZ * l;
        splitFrac[i] = lambda_ * logPart + (1.0f - lambda_) * linPart;
    }

    // Light basis: orthonormal to the sun direction, avoiding a degenerate up.
    glm::vec3 L = glm::normalize(sunDirection);
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(L, worldUp)) > 0.99f) worldUp = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(L, worldUp));
    const glm::vec3 up    = glm::normalize(glm::cross(right, L));

    const glm::vec3 eye = camera.Position();

    for (int i = 0; i < kCascadeCount; ++i) {
        const float dNear = splitFrac[i];
        const float dFar  = splitFrac[i + 1];
        const float tNear = (dNear - nearZ) / (farZ - nearZ);
        const float tFar  = (dFar  - nearZ) / (farZ - nearZ);

        // 8 corners: near/far-plane points lerped along each ray by tNear/tFar.
        glm::vec3 pts[8];
        for (int j = 0; j < 4; ++j) {
            pts[j]     = glm::mix(corners.nearPts[j], corners.farPts[j], tNear);
            pts[4 + j] = glm::mix(corners.nearPts[j], corners.farPts[j], tFar);
        }

        glm::vec3 center(0.0f);
        for (const auto& p : pts) center += p;
        center /= 8.0f;

        float radius = 0.0f;
        for (const auto& p : pts) radius = std::max(radius, glm::distance(center, p));
        radius = std::ceil(radius);   // stabilise quantisation a touch

        // Light-space (view) matrix looking down the sun at the sphere centre.
        const glm::vec3 target = center - L * radius;   // place eye behind sphere
        const glm::vec3 eyePos = center + L * radius;
        glm::mat4 lightView = glm::lookAt(eyePos, target, up);

        const float texelSize = (2.0f * radius) / static_cast<float>(resolution_);
        // Snap the sphere centre to the texel grid in light space (stabilization).
        const glm::vec4 centerLS = lightView * glm::vec4(center, 1.0f);
        const glm::vec3 snapped(
            std::floor(centerLS.x / texelSize) * texelSize,
            std::floor(centerLS.y / texelSize) * texelSize,
            centerLS.z);
        const glm::vec3 diff = snapped - glm::vec3(centerLS);
        lightView = glm::translate(glm::mat4(1.0f), diff) * lightView;

        const glm::mat4 lightProj =
            glm::ortho(-radius, radius, -radius, radius, 0.1f, 2.5f * radius);

        data_.lightMat[i] = lightProj * lightView;
        data_.cascadeSplits[i] = dFar;
        data_.params.y = texelSize;
    }

    (void)eye;
}

void CascadedShadowMap::BeginCascade(int cascade) {
    RenderContext::AssertRenderThread("CascadedShadowMap::BeginCascade");
    fbo_.Bind();
    // Re-point the depth attachment at this cascade layer.
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthArray_.id(), 0, cascade);
    glViewport(0, 0, resolution_, resolution_);
    glClearDepth(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_CLAMP);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 4.0f);
    glCullFace(GL_FRONT);   // front-face culling reduces shadow acne for closed casters
}

void CascadedShadowMap::EndCascade() {
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_CLAMP);
    glCullFace(GL_BACK);   // restore default for the main pass
    fbo_.Unbind();
}

void CascadedShadowMap::Upload() {
    RenderContext::AssertRenderThread("CascadedShadowMap::Upload");
    shadowUbo_.Replace(data_);
}

void CascadedShadowMap::BindUniform() const {
    RenderContext::AssertRenderThread("CascadedShadowMap::BindUniform");
    shadowUbo_.BindBase(kShadowBinding);
}

void CascadedShadowMap::Bind(unsigned unit) const {
    RenderContext::AssertRenderThread("CascadedShadowMap::Bind");
    depthArray_.Bind(unit);
    sampler_.Bind(unit);
}

void CascadedShadowMap::Unbind(unsigned unit) {
    Sampler::Unbind(unit);
    RenderTexture::Unbind(unit);
}

} // namespace gfx
