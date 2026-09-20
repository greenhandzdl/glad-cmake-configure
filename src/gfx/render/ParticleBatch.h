#ifndef GFX_RENDER_PARTICLEBATCH_H
#define GFX_RENDER_PARTICLEBATCH_H

/**
 * @file ParticleBatch.h
 * @brief World-space point-sprite particles: mining debris, splash, feedback
 *        (plan "voxel expansion" C3 — one shader + one dynamic vertex buffer).
 *
 * A deliberately tiny CPU-simulated pool rather than a particle system: each
 * particle carries position/velocity/colour/lifetime, Update(dt) integrates
 * gravity and retires the dead ones by swap-removing (order does not matter —
 * additive blending is commutative), and Draw() submits the survivors as one
 * GL_POINTS array. Point sprites keep the geometry cost at one vertex per
 * particle; the size and the fade come out of the same lifetime ratio in the
 * vertex shader, so no per-frame CPU work beyond the integration.
 *
 * Two-phase convention still applies: Init() (compile + buffers) and every
 * Update/Draw touch GL and are render-thread only. Spawn() is CPU-only, so a
 * worker could queue events through the caller's own plumbing — but note the
 * pool itself is not thread safe, the demo spawns from input callbacks.
 *
 * Sized in world units: glPointSize is a framebuffer-pixel state, so Draw()
 * takes a `pixelScale` (framebufferHeight / (2*tan(fovY/2))) that turns a
 * particle's world diameter into pixels at its depth — the same projection the
 * scene uses, so particles keep their apparent size across window sizes and
 * Retina backing scales.
 */

#include <cstdint>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "gfx/core/GLBuffer.h"
#include "gfx/core/VertexArray.h"
#include "gfx/shader/ShaderProgram.h"

namespace gfx {

class Texture2D;

class ParticleBatch {
public:
    // One pooled particle. `age/life` (0..1) drives size + fade in the shader.
    struct Particle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 color{1.0f};
        float life = 1.0f;
        float age = 0.0f;
        float size = 0.12f;        // world-space diameter at birth
        float gravity = 9.8f;      // per-particle so debris drops and smoke floats
        float drag = 0.6f;
    };

    bool Init();   // compile shader + VAO/VBO. Render-thread.

    // Queue one particle; `size` is a world-space diameter. Returns false when
    // the pool is full (cap is a hard ceiling, particles are cosmetic).
    bool Spawn(const glm::vec3& position, const glm::vec3& velocity,
               const glm::vec4& color, float life, float size = 0.12f,
               float gravity = 9.8f);

    // Integrate + retire. Call once per frame with the frame's dt (seconds).
    void Update(float dt);

    // Draw the live particles into the currently bound target with the given
    // view-projection. Depth test on, depth write off, alpha blend (particles
    // never occlude each other), and GL_PROGRAM_POINT_SIZE enabled - the shader
    // sizes points with gl_PointSize, which the driver ignores while that cap is
    // off (the default on macOS), collapsing every particle to one pixel. The
    // state is restored on exit. `tex` may be null for square points.
    // pixelScale = fbHeight / (2*tan(fovY/2)). Render-thread.
    void Draw(const glm::mat4& viewProj, const glm::vec3& cameraPos,
              float pixelScale, const Texture2D* tex = nullptr);

    [[nodiscard]] std::size_t count() const noexcept { return live_; }
    [[nodiscard]] bool valid() const noexcept { return ready_; }
    void Clear() noexcept { live_ = 0; }

    static constexpr std::size_t kCapacity = 2048;

private:
    struct GpuVertex {
        glm::vec3 position;
        float     t;         // age / life
        glm::vec4 color;
        float     size;
        float     pad[3];
    };

    ShaderProgram shader_;
    VertexArray   vao_;
    GLBuffer      vbo_;
    std::vector<Particle> particles_;
    std::vector<GpuVertex> stage_;
    std::size_t live_ = 0;
    bool ready_ = false;
};

} // namespace gfx

#endif // GFX_RENDER_PARTICLEBATCH_H
