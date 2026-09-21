/**
 * @file main.cpp
 * @brief particles - a CPU-simulated point-sprite fountain.
 *
 * ParticleBatch is a deliberately small pool (cap 2048): Spawn() queues a
 * world-space particle (position/velocity/colour/life/size/gravity) on any
 * thread, Update(dt) integrates gravity + drag and swap-removes the dead, and
 * Draw() submits the survivors as a single GL_POINTS array. The batch is
 * normally driven by VoxelTransparentPass; here a tiny custom pass owns it and
 * drives it standalone so the point-sprite path is shown on its own.
 *
 * The one contract worth flagging: glPointSize is a framebuffer-pixel quantity,
 * so Draw() takes a `pixelScale` (fbHeight / (2*tan(fovY/2))) that turns each
 * particle's world diameter into pixels at its depth - the same projection the
 * scene uses, so the fountain keeps its apparent size across window and Retina
 * scales. The shader sizes points via gl_PointSize, which the driver ignores
 * unless GL_PROGRAM_POINT_SIZE is on (ParticleBatch::Draw enables it) - otherwise
 * every particle collapses to one pixel on macOS.
 *
 * Controls: click drags the emitter, Esc quits, --quit-after SECONDS for headless.
 */

#include "demo/demo_app.h"

#include <cmath>
#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {

// A pass that owns the batch, spawns a steady fountain, integrates, and draws
// straight at the default framebuffer (no post chain, no scene graph).
class ParticlePass : public gldx::RenderPass {
public:
    ParticlePass() : RenderPass("Particles") {
        if (!batch_.Init()) {
            std::fprintf(stderr, "ParticleBatch init failed\n");
            return;
        }
        ready_ = true;
    }

    void Execute(gldx::RenderFrame& f) override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, f.fbWidth, f.fbHeight);
        glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!ready_ || !f.camera) return;

        // A fountain: a short burst every frame from the origin, aimed up with a
        // little spread, warm colours fading to ember-red over their lifetime.
        for (int i = 0; i < 24; ++i) {
            const float a = uniform(-3.14159265f, 3.14159265f);
            const float r = uniform(0.0f, 0.5f);
            const glm::vec3 vel(std::cos(a) * r, uniform(3.5f, 5.5f), std::sin(a) * r);
            const float t = uniform(0.0f, 1.0f);
            const glm::vec4 col(1.0f, 0.45f + 0.4f * t, 0.15f * t, 1.0f);
            batch_.Spawn(glm::vec3(0.0f, 0.2f, 0.0f), vel, col,
                         uniform(1.2f, 2.4f), uniform(0.05f, 0.14f), 9.8f);
        }
        batch_.Update(static_cast<float>(lastDt_));

        const glm::vec3 eye = f.camera->Position();
        const float fovRad = glm::radians(f.camera->FovY());
        const float pixelScale = static_cast<float>(f.fbHeight) / (2.0f * std::tan(fovRad * 0.5f));
        batch_.Draw(f.viewProj, eye, pixelScale, nullptr);
    }

    void setDt(double dt) noexcept { lastDt_ = dt; }
    [[nodiscard]] std::size_t live() const noexcept { return batch_.count(); }

private:
    static float uniform(float lo, float hi) {
        const double u = static_cast<double>(std::rand()) / (RAND_MAX + 1.0);
        return lo + static_cast<float>(u) * (hi - lo);
    }

    gldx::ParticleBatch batch_;
    bool ready_ = false;
    double lastDt_ = 0.0;
};

// Auto-framing orbit (no mouse needed): the fountain sits at the origin.
glm::vec3 orbitEye(float time) {
    const float yaw = time * 0.25f;
    const float pitch = 0.35f;
    const float radius = 6.0f;
    const float cp = std::cos(pitch);
    return {radius * cp * std::sin(yaw), radius * std::sin(pitch), radius * cp * std::cos(yaw)};
}

} // namespace

int main(int argc, char** argv) {
    const demo::Flags flags(argc, argv, {}, {}, "particles");
    if (flags.wantsHelp()) { flags.printUsage(); return 0; }

    return demo::Run(flags, "gldx demo - particles (point-sprite fountain)",
                     [&](demo::Ctx& ctx) -> int {
        auto pass = std::make_unique<ParticlePass>();
        ParticlePass* raw = pass.get();
        ctx.renderer.AddPass(std::move(pass));

        gldx::Camera camera; camera.SetPerspective(50.0f, 1.0f, 0.1f, 100.0f);

        return ctx.Loop([&](gldx::RenderFrame& f, const demo::FrameInfo& info) {
            const glm::vec3 eye = orbitEye(static_cast<float>(info.time));
            camera.SetViewportAspect(info.fbHeight > 0 ? static_cast<float>(info.fbWidth) / info.fbHeight : 1.0f);
            camera.LookAt(eye, glm::vec3(0.0f, 1.2f, 0.0f), glm::vec3(0, 1, 0));

            raw->setDt(info.dt);
            f.camera = &camera;
            f.viewProj = camera.ViewProjection();
        });
    });
}
