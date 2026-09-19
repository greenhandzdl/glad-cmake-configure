#ifndef GFX_RENDER_SKYBOXRENDERER_H
#define GFX_RENDER_SKYBOXRENDERER_H

/**
 * @file SkyboxRenderer.h
 * @brief Draws the HDR environment cube as the opaque background (plan "Skybox").
 *
 * Owns the skybox shader and a unit cube mesh (both GL resources, created in
 * Init() on the render thread). Draw() binds a caller-supplied cube map — the
 * EnvironmentMap's source sky — and renders it with the LEQUAL-depth trick so
 * the skybox only fills pixels where nothing else was drawn.
 */

#include <glm/glm.hpp>

#include "gfx/geometry/Mesh.h"
#include "gfx/shader/ShaderProgram.h"
#include "gfx/texture/TextureCubeMap.h"

namespace gfx {

class SkyboxRenderer {
public:
    SkyboxRenderer() = default;

    // Compile the shader + upload the cube. Render-thread only.
    // Returns false (and logs) if the shader fails to build.
    bool Init();

    // Render `cube` as the background for the given view-projection. Bind to a
    // texture unit that the caller leaves free for the skybox (default the
    // dedicated skybox unit). Render-thread only.
    void Draw(const glm::mat4& viewProj, const TextureCubeMap& cube,
              unsigned unit) const;

    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    ShaderProgram shader_;
    Mesh          cube_;
    bool          ready_ = false;
};

} // namespace gfx

#endif // GFX_RENDER_SKYBOXRENDERER_H
