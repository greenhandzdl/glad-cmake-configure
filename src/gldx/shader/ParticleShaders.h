#ifndef GLDX_SHADER_PARTICLESHADERS_H
#define GLDX_SHADER_PARTICLESHADERS_H

/**
 * @file ParticleShaders.h
 * @brief Embedded GLSL (410 core) for the world-space point-sprite batch.
 *
 * One vertex per particle; the point sprite is expanded by the rasteriser, so
 * a debris puff costs no geometry and no index buffer. Size and alpha both come
 * from the normalised age so the CPU only integrates motion.
 *
 * Uniform conventions (all per-draw):
 *   uViewProj    - the scene's view-projection, shared with the voxel passes
 *   uCamPos      - eye position in world space (point size needs the distance)
 *   uPixelScale  - fbHeight / (2*tan(fovY/2)), converts world size to pixels
 *   uSizeBoost   - global multiplier for taste / difficulty of visibility
 *   uTex         - optional soft dot; when uTextured == 0 the point is a solid
 *                  square, which reads fine for 2-4 px debris chips
 *
 * No lighting and no fog: particles live for a fraction of a second next to
 * the crosshair where a voxel block was just broken, so the caller bakes the
 * block colour into the vertex and the cheap look is the correct one.
 */

#include <glad/gl.h>

namespace gldx::shaders {

inline const char* kParticleVertex = R"GLSL(#version 410 core
layout(location=0) in vec3  aPos;
layout(location=1) in float aT;        // age / life, 0..1
layout(location=2) in vec4  aColor;
layout(location=3) in float aSize;     // world-space diameter at birth

uniform mat4  uViewProj;
uniform vec3  uCamPos;
uniform float uPixelScale;
uniform float uSizeBoost;

out vec4  vColor;
out float vT;

void main() {
    vec4 clip = uViewProj * vec4(aPos, 1.0);
    float dist = max(length(uCamPos - aPos), 1e-3);
    // Shrink with distance (perspective), then fade out over the lifetime; the
    // clamp keeps a far particle at least one pixel and a near one under the
    // driver's point-size limit (128 here is below every GL 4.1 implementation).
    gl_PointSize = clamp(aSize * uPixelScale / dist * uSizeBoost, 1.0, 128.0);
    vColor = aColor;
    vT = aT;
    gl_Position = clip;
}
)GLSL";

inline const char* kParticleFragment = R"GLSL(#version 410 core
in vec4  vColor;
in float vT;
out vec4 FragColor;

uniform sampler2D uTex;
uniform int uTextured;

void main() {
    float a = vColor.a * (1.0 - vT * vT);   // ease-out fade
    if (uTextured == 1) a *= texture(uTex, gl_PointCoord).a;
    if (a < 0.01) discard;
    // Linear HDR like every other scene program; the composite tone-maps it.
    FragColor = vec4(vColor.rgb, a);
}
)GLSL";

} // namespace gldx::shaders

#endif // GLDX_SHADER_PARTICLESHADERS_H
