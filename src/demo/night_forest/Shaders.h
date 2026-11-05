#ifndef NIGHT_FOREST_SHADERS_H
#define NIGHT_FOREST_SHADERS_H

/**
 * @file Shaders.h
 * @brief The night_forest GLSL: one program for the whole world, shaded with a
 *        Gooch sketching light model instead of a physically-based one.
 *
 * Everything the demo draws - the ground, both tree species, the crate - is an
 * InstancedMesh, so one vertex stage serves all of them: positions / normals /
 * uv and the per-vertex colour from the geometry buffer (locations 0, 1, 2 and
 * 9), the per-instance matrix and tint from the instance buffer (4-7 and 8).
 * That is the whole reason the demo exists as an instancing showcase: a 1500
 * tree forest is three draw calls, not fifteen hundred, and the trunk brown and
 * canopy green come from the mesh rather than from a second instance array.
 *
 * Why Gooch rather than the engine's PBR program: this scene is lit by a moon
 * and a handful of lanterns at night, and a physically-based response to a
 * 0.03-intensity light is a black silhouette - the opposite of readable. Gooch's
 * directional model (Gooch et al., "A Sketching Directional Light Model") was
 * made for exactly that: the terminator is an *edge* between two flat tones
 * rather than a cosine ramp, the shadow tone is a tint rather than zero, and the
 * highlight is added and desaturated instead of multiplied. Here the shadow tone
 * is pushed cold (a blue-violet lift the unlit forest floor never stops having)
 * and the lit tone warm, so a lantern pool reads as warmth against the night
 * instead of as "less black".
 *
 * The fragment program keeps one uniform block that the engine already owns -
 * LightingBlock, packed by gldx::PackLighting and bound at
 * LightBuffer::kBinding. Its member *names* differ from kPbrFragment's; only the
 * std140 layout has to agree, and it does (every member is a 16-byte vec4).
 *
 * The crate is the only object with uEnvMix > 0, and uEnvMix is a runtime value
 * while the samplerCube is a static declaration - so uEnvCube is an *active*
 * sampler for every draw of this program, including the trees. On Apple's GL an
 * active sampler on a unit without a type-correct, complete texture makes the
 * whole call INVALID_OPERATION and the driver drops every triangle, which is why
 * Passes.cpp binds a real cube to that unit unconditionally rather than only
 * when the capture is switched on.
 *
 * NOTE: raw GLSL only - no gldx types here, so this header may be included
 * before or after `import gldx;`.
 */

#include <string>

namespace night_forest::shaders {

// Common header for the fragment stages: the engine's lighting block plus the
// Gooch band function. Split out because three entry points need identical
// arithmetic, and a shading model that disagrees between the ground and the
// trees is the mistake this file exists to avoid.
inline const std::string kGoochGlsl = R"GLSL(#version 410 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
in vec4 vColor;
out vec4 FragColor;

struct PL { vec4 posRange; vec4 colorIntensity; };
layout(std140) uniform LightingBlock {
    vec4  cameraPos;
    vec4  dirColor;       // moon rgb, intensity in w
    vec4  dirDirection;   // moon travel direction xyz
    vec4  ambientTint;
    ivec4 pointCount;
    PL points[32];
    vec4  fogColor;       // linear HDR rgb + pad
    vec4  fogParams;      // x enabled, y start, z end
};

uniform vec3  uLightTint;     // the warm half of the band pair
uniform vec3  uShadowTint;    // the cold half, multiplied into the albedo
uniform vec3  uShadowLift;    // the cold floor added where the band says "shade"
uniform float uBandWidth;     // half-width of the terminator, in remapped units
uniform float uSpecPower;
uniform float uSpecStrength;

// Where a surface sits in the band, on its own so the caller can spend it twice.
// Remapped into [0.1, 1] first: nothing is ever fully dark, which is the whole
// reason a sketch reads as a drawing rather than as a hole in the picture.
float GoochT(vec3 N, vec3 L) {
    float k = clamp(0.1 + 0.9 * dot(N, L), 0.0, 1.0);
    return smoothstep(0.5 - uBandWidth, 0.5 + uBandWidth, k);
}

// One directional or point light, Gooch-style: pick a flat tone from each end of
// the remapped N.L and blend between them over a band narrow enough to read as an
// edge. The highlight is added on top and desaturated - under a multiplied
// specular a night scene loses its only bright pixels, and the sketch reading
// depends on the glint being white rather than the colour of whatever it is
// sitting on.
//
// Note what is *not* here: the shadow lift. It belongs to the surface, not to a
// light, and this function runs once per light - folding it in would add four
// copies of it under the lanterns and, worse, scale it by the moon's intensity,
// so dimming the moon would fade the cold tone back out to plain black.
vec3 GoochBand(vec3 albedo, vec3 N, vec3 L, vec3 V, vec3 lc) {
    float t = GoochT(N, L);
    vec3 base = albedo * mix(uShadowTint, uLightTint, t) * lc;

    vec3 H = normalize(L + V);
    float s = pow(max(dot(N, H), 0.0), uSpecPower) * step(0.5, t);
    // The glint is a grey, not the light's colour: Gooch's own paper greys its
    // specular for the same reason a pen does. A touch of blue keeps it from
    // reading as a daytime highlight.
    base += vec3(s) * uSpecStrength * lc * vec3(0.72, 0.78, 0.95);
    return base;
}

// Night mist rather than daytime haze: the far plane dissolves the tree line
// into the same cold tone the shadows carry, so distance and shade stop being
// two different greys.
vec3 ApplyFog(vec3 color, vec3 worldPos) {
    if (fogParams.x < 0.5) return color;
    float dist = length(cameraPos.xyz - worldPos);
    float t = clamp((dist - fogParams.y) / max(fogParams.z - fogParams.y, 1e-4), 0.0, 1.0);
    return mix(color, fogColor.rgb, t);
}
)GLSL";

// ---------------------------------------------------------------------------
// The single world program: instanced geometry, Gooch lighting, an optional
// albedo map and an optional environment-cube reflection + refraction.
// ---------------------------------------------------------------------------
inline const std::string kWorldVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=4) in mat4 aInstance;   // consumes locations 4,5,6,7
layout(location=8) in vec4 aInstColor;
layout(location=9) in vec4 aVertexColor;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out vec4 vColor;

void main() {
    vec4 wp = aInstance * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    // Rigid instance transform only (rotation about Y + one uniform scale), so
    // the upper 3x3 with normalise() IS the normal matrix. Scene.cpp keeps to
    // that contract: any non-uniform stretch has to be baked into the mesh,
    // where the inverse-transpose is available on the CPU.
    vNormal = normalize(mat3(aInstance) * aNormal);
    vUv = aUv;
    // Two colours, multiplied: the instance tints a whole tree (a pale birch
    // against a dark fir), the vertex colour says which part of it is bark.
    vColor = aInstColor * aVertexColor;
    gl_Position = uViewProj * wp;
}
)GLSL";

inline const std::string kWorldFragment = kGoochGlsl + R"GLSL(
uniform sampler2D uAlbedo;      // unit 0
uniform samplerCube uEnvCube;   // unit 11 - always backed by a complete cube
uniform float uUseTex;          // 0 = flat vColor, 1 = multiply by the map
uniform vec2  uUvScale;         // texture repeat (the ground tiles, the crate does not)
uniform float uEnvMix;          // 0 for everything but the crate
uniform float uEta;             // cos(theta_i)/cos(theta_r) ratio for refract()
uniform float uRefract;         // 0 = reflect only, 1 = refract where possible
uniform float uEnvLod;          // mip level of the captured cube: a soft reflection
                                // on wood, 0 for a mirror on the fallback path
uniform float uEmissive;        // lanterns: HDR boost, scaled by the vertex mask

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(cameraPos.xyz - vWorldPos);
    vec3 albedo = vColor.rgb;
    if (uUseTex > 0.5) albedo *= texture(uAlbedo, vUv * uUvScale).rgb;

    // ---- moon: the one directional light, so the band pair is the scene's ----
    // ---- warm/cool spine. ----------------------------------------------------
    vec3 Lm = normalize(-dirDirection.xyz);
    float tMoon = GoochT(N, Lm);
    vec3 color = GoochBand(albedo, N, Lm, V, dirColor.rgb * dirColor.w);

    // ---- lanterns -------------------------------------------------------------
    // Each point light adds a band of its own rather than feeding an ambient
    // term, which is what keeps the pool on the ground next to a crate warm and
    // the trees ten metres past it cold. Farther than the light's range the band
    // has to end exactly rather than fade, so a tree trunk stops being lit at a
    // visible edge - a sketch contour, not a smooth falloff.
    for (int i = 0; i < pointCount.x; ++i) {
        vec3 d = points[i].posRange.xyz - vWorldPos;
        float dist = length(d);
        float range = max(points[i].posRange.w, 1e-4);
        if (dist > range) continue;
        float att = 1.0 - dist / range;
        att *= att;
        vec3 lc = points[i].colorIntensity.rgb * points[i].colorIntensity.w;
        color += GoochBand(albedo, N, d / max(dist, 1e-4), V, lc) * att;
    }

    // The cold floor, added once and outside every light's scale: this is what
    // the unlit side of a surface *is*, not how much moonlight it is missing, so
    // it survives a dim moon and a lantern's shadow side stays a colour rather
    // than a hole. The `(1 - tMoon)` gate keeps it on the shade side of the
    // terminator only - a lit surface that also carried it would be washed grey.
    color += uShadowLift * (1.0 - tMoon);
    // Nothing in range of any light still gets the ambient floor on top, so the
    // scene has no pure black in it - the one property a night scene has to keep
    // or the silhouette stops being readable.
    color += albedo * ambientTint.xyz;

    // ---- the environment cube, for the crate only -----------------------------
    // Reflection and refraction are both pulled from the same captured cube;
    // which one wins at a pixel is the Fresnel curve, so the box mirrors at its
    // grazing edges and shows what is behind it head-on. uEnvMix stays low (the
    // demo's default is 0.18): at 0.5 the cube map is a mirror and the wood
    // grain, the wander and the whole instancing story disappear behind it.
    //
    // The vertex alpha gates it, which is why the strength is a per-draw uniform
    // and the permission is per-vertex: the crate wants its whole surface in, and a
    // lantern that also mirrored the sky would be a second, unlit copy of a thing
    // that is already bright.
    //
    // textureLod rather than texture(): a cube sampled by direction has no
    // meaningful screen-space derivative, so implicit LOD would pick a level from
    // how fast the direction changes between neighbouring pixels - which is a
    // property of the camera, not of the wood.
    if (uEnvMix > 0.0 && vColor.a > 0.0) {
        vec3 R = reflect(-V, N);
        vec3 T = refract(-V, N, uEta);
        if (dot(T, T) < 1e-6) T = R;          // total internal reflection
        float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        vec3 env = mix(textureLod(uEnvCube, T, uEnvLod).rgb,
                       textureLod(uEnvCube, R, uEnvLod).rgb,
                       mix(0.25, 0.85, fres) * uRefract + (1.0 - uRefract));
        color = mix(color, color * 0.25 + env, uEnvMix * vColor.a);
    }

    // The mask is why the lantern's glass is one mesh with its post: a uniform
    // says how strong the glow is, the vertex says which triangles get it.
    color += albedo * uEmissive * vColor.a;

    FragColor = vec4(ApplyFog(color, vWorldPos), 1.0);   // linear HDR
}
)GLSL";

} // namespace night_forest::shaders

#endif // NIGHT_FOREST_SHADERS_H
