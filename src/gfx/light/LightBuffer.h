#ifndef GFX_LIGHT_LIGHTBUFFER_H
#define GFX_LIGHT_LIGHTBUFFER_H

/**
 * @file LightBuffer.h
 * @brief Uniform-buffer batch for the scene lights (plan "LightBuffer").
 *
 * Owns one UniformBuffer sized to LightingBlockGpu and re-uploads the packed
 * data once per frame. Bound to a fixed UBO binding point (kBinding); because
 * GLSL 4.10 forbids layout(binding=...) on blocks, consumers map their
 * LightingBlock onto that point via ShaderProgram::SetBlockBinding.
 */

#include "gfx/core/UniformBuffer.h"
#include "gfx/light/Light.h"

namespace gfx {

class LightBuffer {
public:
    static constexpr GLuint kBinding = 1;

    // Create the backing store. Render-thread only.
    void Init() {
        ubo_.Create(static_cast<GLsizeiptr>(sizeof(LightingBlockGpu)), GL_DYNAMIC_DRAW);
    }

    // Repack + re-upload. Render-thread only.
    void Update(const LightSetup& setup, const glm::vec3& cameraPos) {
        const LightingBlockGpu block = PackLighting(setup, cameraPos);
        ubo_.Replace(block);
    }

    void Bind() const { ubo_.BindBase(kBinding); }

    [[nodiscard]] bool valid() const noexcept { return ubo_.valid(); }

private:
    UniformBuffer ubo_;
};

} // namespace gfx

#endif // GFX_LIGHT_LIGHTBUFFER_H
