#ifndef GLDX_VOXEL_VOXELSHADERS_H
#define GLDX_VOXEL_VOXELSHADERS_H

/**
 * @file VoxelShaders.h
 * @brief Embedded GLSL (410 core) for the chunked voxel path: texture-array
 *        sampling + baked vertex light + sun/ambient + linear distance fog.
 *
 * Deliberately NOT the PBR program: voxel terrain wants a flat, unlit-look
 * shade where the baked AO/sky term does most of the work, so one cheap
 * Lambert sun term is enough and every chunk face stays two triangles flat.
 * The face normal is recovered from screen-space derivatives of the world
 * position, which removes the normal attribute entirely (axis-aligned faces
 * give exact results) — the voxel vertex is 48 B and will not grow.
 *
 * Uniform conventions (all per-draw unless noted):
 *   uModel / uViewProj   - chunk transform chain
 *   uAtlas               - sampler2DArray block textures (bound by the pass)
 *   uAlphaCutoff         - > 0 enables cutout discard (leaves)
 *   uOverrideAlpha       - scales sampled alpha (water pass uses ~0.55)
 *   uUvScroll            - animated UV offset for the transparent variant
 *   uTime                - seconds, feeds the scroll
 * LightingBlock (binding 1) supplies sun colour/intensity/direction, ambient
 * and the fog parameters (see gldx/light/Light.h).
 */

#include <glad/gl.h>

namespace gldx::shaders {

inline const char* kVoxelVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in float aLayer;
layout(location=3) in float aLight;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform vec2 uUvScroll;

out vec3  vWorldPos;
out vec2  vUV;
out float vLayer;
out float vLight;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vUV       = aUV + uUvScroll;
    vLayer    = aLayer;
    vLight    = aLight;
    gl_Position = uViewProj * wp;
}
)GLSL";

inline const char* kVoxelFragment = R"GLSL(#version 410 core
in vec3  vWorldPos;
in vec2  vUV;
in float vLayer;
in float vLight;
out vec4 FragColor;

struct PL { vec4 posRange; vec4 colorIntensity; };
layout(std140) uniform LightingBlock {
    vec4  cameraPos;
    vec4  dirColor;       // sun rgb, intensity in w
    vec4  dirDirection;   // sun travel direction xyz
    vec4  ambientTint;
    ivec4 pointCount;
    PL points[32];
    vec4  fogColor;       // linear HDR rgb + pad
    vec4  fogParams;      // x enabled, y start, z end
};

uniform sampler2DArray uAtlas;
uniform float uAlphaCutoff;
uniform float uOverrideAlpha;

vec3 ApplyFog(vec3 color, vec3 worldPos) {
    if (fogParams.x < 0.5) return color;
    float dist = length(cameraPos.xyz - worldPos);
    float t = clamp((dist - fogParams.y) / max(fogParams.z - fogParams.y, 1e-4), 0.0, 1.0);
    return mix(color, fogColor.rgb, t);
}

void main() {
    vec4 albedo = texture(uAtlas, vec3(vUV, vLayer));
    float alpha = albedo.a * uOverrideAlpha;
    if (alpha < uAlphaCutoff) discard;

    // Exact face normal from the world-pos derivatives (flat axis-aligned
    // quads): sign follows the rasterised face, so it always faces the viewer.
    vec3 N = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
    vec3 L = normalize(-dirDirection.xyz);
    float ndl = max(dot(N, L), 0.0);

    // Baked sky/AO carries the shape; the sun adds a gentle lift only, so
    // shaded sides never crush to black and the look stays blocky-classic.
    vec3 sun = dirColor.rgb * dirColor.w;
    vec3 lightColor = ambientTint.rgb + sun * (0.25 + 0.75 * ndl);
    vec3 color = albedo.rgb * vLight * lightColor;

    FragColor = vec4(ApplyFog(color, vWorldPos), alpha);   // linear HDR; composite tone-maps
}
)GLSL";

} // namespace gldx::shaders

#endif // GLDX_VOXEL_VOXELSHADERS_H
