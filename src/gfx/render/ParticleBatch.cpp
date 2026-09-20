module;

#include "gfx/gmf.hpp"

module gfx;

namespace gfx {

bool ParticleBatch::Init() {
    RenderContext::AssertRenderThread("ParticleBatch::Init");

    auto p = ShaderProgram::CreateFromSource(shaders::kParticleVertex,
                                            shaders::kParticleFragment);
    if (!p) return false;
    shader_ = std::move(*p);

    vao_.Create();
    vbo_.Reserve(GL_ARRAY_BUFFER, kCapacity * sizeof(GpuVertex), GL_DYNAMIC_DRAW);

    vao_.Bind();
    vbo_.Bind(GL_ARRAY_BUFFER);
    constexpr GLsizei stride = sizeof(GpuVertex);
    vao_.AttachAttribute(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    vao_.AttachAttribute(1, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(12));
    vao_.AttachAttribute(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));
    vao_.AttachAttribute(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(32));
    vbo_.Unbind(GL_ARRAY_BUFFER);
    vao_.Unbind();

    particles_.reserve(kCapacity);
    stage_.reserve(kCapacity);
    ready_ = true;
    return true;
}

bool ParticleBatch::Spawn(const glm::vec3& position, const glm::vec3& velocity,
                          const glm::vec4& color, float life, float size,
                          float gravity) {
    if (live_ >= kCapacity || life <= 0.0f) return false;   // pool is a ceiling

    Particle p;
    p.position = position;
    p.velocity = velocity;
    p.color = color;
    p.life = life;
    p.age = 0.0f;
    p.size = size;
    p.gravity = gravity;
    // Drag is fixed rather than a parameter: it is the one term that keeps
    // debris from looking like a ballistic test, and callers tune colour,
    // lifetime and speed instead.
    p.drag = 0.6f;

    if (live_ < particles_.size()) particles_[live_] = p;
    else particles_.push_back(p);
    ++live_;
    return true;
}

void ParticleBatch::Update(float dt) {
    RenderContext::AssertRenderThread("ParticleBatch::Update");
    if (dt <= 0.0f) return;

    for (std::size_t i = 0; i < live_; ) {
        Particle& p = particles_[i];
        p.age += dt;
        if (p.age >= p.life) {
            // Swap-remove: draw order is irrelevant under alpha blending of
            // depth-tested points, so packing the array beats a stable erase.
            p = particles_[live_ - 1];
            --live_;
            continue;
        }
        const float decay = std::exp(-p.drag * dt);
        p.velocity = p.velocity * decay + glm::vec3(0.0f, -p.gravity, 0.0f) * dt;
        p.position += p.velocity * dt;
        ++i;
    }
}

void ParticleBatch::Draw(const glm::mat4& viewProj, const glm::vec3& cameraPos,
                         float pixelScale, const Texture2D* tex) {
    RenderContext::AssertRenderThread("ParticleBatch::Draw");
    if (!ready_ || live_ == 0) return;

    stage_.clear();
    for (std::size_t i = 0; i < live_; ++i) {
        const Particle& p = particles_[i];
        GpuVertex v;
        v.position = p.position;
        v.t = p.age / p.life;
        v.color = p.color;
        // Tail-end shrink reads as "absorbed" instead of "switched off".
        v.size = p.size * (1.0f - 0.5f * v.t * v.t);
        stage_.push_back(v);
    }

    // Own the states this primitive needs and restore them: the transparent
    // voxel pass may already have left blending on with depth writes off, and
    // whatever runs next must not inherit a surprise (a leaked GL_DEPTH_TEST
    // disable once blanked a later pass — see the HUD/Shadow note in
    // DebugDraw.cpp).
    GLboolean hadDepthMask = GL_TRUE;
    GLboolean depthTest = GL_FALSE;
    GLboolean blend = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &hadDepthMask);
    glGetBooleanv(GL_DEPTH_TEST, &depthTest);
    glGetBooleanv(GL_BLEND, &blend);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_.Use();
    shader_.Set("uViewProj", viewProj);
    shader_.Set("uCamPos", cameraPos);
    shader_.Set("uPixelScale", pixelScale);
    shader_.Set("uSizeBoost", 1.0f);
    shader_.Set("uTex", static_cast<int>(texunit::particle));
    shader_.Set("uTextured", tex && tex->valid() ? 1 : 0);
    if (tex && tex->valid()) tex->Bind(texunit::particle);

    vao_.Bind();
    vbo_.SubData(std::span<const GpuVertex>(stage_));
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(stage_.size()));
    vao_.Unbind();

    glDepthMask(hadDepthMask ? GL_TRUE : GL_FALSE);
    if (!depthTest) glDisable(GL_DEPTH_TEST);
    if (!blend) glDisable(GL_BLEND);
}

} // namespace gfx
