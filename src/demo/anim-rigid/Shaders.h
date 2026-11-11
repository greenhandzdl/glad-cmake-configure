#ifndef ANIM_RIGID_SHADERS_H
#define ANIM_RIGID_SHADERS_H

/**
 * @file Shaders.h
 * @brief The two GLSL 410 programs anim-rigid draws with.
 *
 * Demo-local on purpose: gldx::shaders::kPbrFragment is the engine's full PBR
 * material and needs the shadow array + IBL cube + LightingBlock UBO bound
 * before a single draw is legal (an active sampler pointing at nothing makes
 * Apple's driver drop the whole call). This demo has one sun, no environment
 * map and no shadow map, so it carries the small shader it can actually back.
 *
 * The skinning contract, spelled out because both halves have to agree:
 *   - attributes 0/1/2 are position/normal/uv, 5/6 are the bone indices
 *     (integer attribute, never normalised) and the four weights
 *     (SkinnedVertex.h, SkinnedMesh.h assign exactly these locations);
 *   - uBones[] is the per-bone palette Skeleton::ComputeSkinning produced for
 *     this frame, and it already carries the model transform (orientation,
 *     scale, forward offset), so the vertex shader's output *is* world space;
 *   - the array is sized by gldx::kMaxBonePalette, and a static_assert in
 *     main.cpp pins the two numbers together so a rig that outgrows the GLSL
 *     fails to build instead of reading garbage.
 */

namespace anim_rigid::shaders {

// Skinned, single-sun PBR. ACES + gamma at the end because the pass renders
// straight to the default framebuffer (no PostProcessChain to encode for it).
inline constexpr const char* kSkinVertex = R"GLSL(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 5) in uvec4 aBoneIndex;
layout(location = 6) in vec4 aBoneWeight;

uniform mat4 uBones[64];
uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;

void main() {
    // Linear blend skinning: the four influences are already normalised to sum
    // to 1 by the loader, so this is the weighted average of four rigid maps.
    mat4 skin = aBoneWeight.x * uBones[aBoneIndex.x]
              + aBoneWeight.y * uBones[aBoneIndex.y]
              + aBoneWeight.z * uBones[aBoneIndex.z]
              + aBoneWeight.w * uBones[aBoneIndex.w];

    vec4 world = skin * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    // mat3(skin) is only a rotation+uniform-scale block (every bone matrix is
    // rigid times the same root scale), so normalising afterwards is exact.
    vNormal = normalize(mat3(skin) * aNormal);
    vUv = aTexCoord;
    gl_Position = uViewProj * world;
}
)GLSL";

inline constexpr const char* kSkinFragment = R"GLSL(
#version 410 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;

uniform vec3  uCamPos;
uniform vec3  uSunDir;          // direction the light travels through the scene
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform vec3  uSkyColor;
uniform vec3  uBounceColor;

// The FBX embeds its four maps, so they are real textures rather than a stand-in
// material - but every sampler has to be backed by a unit even when the file has
// no such map, because an active sampler pointing at nothing makes Apple's
// driver drop the whole draw call (the failure looks like "the model vanished").
// The demo binds 1x1 dummies and switches the *meaning* with these flags.
uniform sampler2D uAlbedo;
uniform sampler2D uNormal;
uniform sampler2D uMetallic;
uniform sampler2D uRoughness;
uniform int   uHasAlbedo;
uniform int   uHasNormal;
uniform int   uHasMetallic;
uniform int   uHasRoughness;
uniform vec4  uBaseColor;
uniform float uMetallicFactor;
uniform float uRoughnessFactor;

out vec4 FragColor;

const float PI = 3.14159265359;

vec3 AcesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}

// Height-correlated Smith visibility: the 1/(4 NoV NoL) term is already folded
// in, so the specular term below must not divide by it again.
float V_Smith(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 F_Schlick(float u, vec3 f0) {
    return f0 + (vec3(1.0) - f0) * pow(1.0 - u, 5.0);
}

// Tangent frame from screen-space derivatives (Christian Schueler's cotangent
// frame). The asset ships no tangents, and deriving them here costs three
// derivatives instead of 12 more floats per vertex - which matters because the
// UV layout of a 17-part body is continuous per part, not per mesh.
mat3 CotangentFrame(vec3 N, vec3 p, vec2 uv) {
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}

void main() {
    vec3 N = normalize(vNormal);
    if (uHasNormal != 0) {
        vec3 sampled = texture(uNormal, vUv).xyz * 2.0 - 1.0;
        N = normalize(CotangentFrame(N, vWorldPos, vUv) * sampled);
    }

    vec3 albedo = uBaseColor.rgb;
    if (uHasAlbedo != 0) {
        // The map is uploaded as sRGB, so sampling already returns linear values.
        albedo *= texture(uAlbedo, vUv).rgb;
    }
    float metallic = uMetallicFactor;
    if (uHasMetallic != 0) metallic *= texture(uMetallic, vUv).b;
    float roughness = uRoughnessFactor;
    if (uHasRoughness != 0) roughness *= texture(uRoughness, vUv).g;
    // Clamped below 1: a perfectly smooth surface turns the sun into a
    // sub-pixel firefly, and this demo has no temporal accumulation to hide it.
    roughness = clamp(roughness, 0.06, 1.0);

    vec3 V = normalize(uCamPos - vWorldPos);
    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(L + V);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 1e-4);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    float a = roughness * roughness;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F_Schlick(VoH, f0);
    vec3 spec = D_GGX(NoH, a) * V_Smith(NoV, NoL, a) * F;
    vec3 diffuse = albedo * (1.0 - metallic) * (1.0 - F) / PI;
    vec3 direct = uSunColor * uSunIntensity * NoL * (diffuse + spec);

    // Hemisphere fill, tinted by the floor bounce on the underside. A skin needs
    // light from somewhere other than the key light or the shadowed side goes
    // to zero and the silhouette reads as a cut-out.
    vec3 ambient = albedo * mix(uBounceColor, uSkyColor, N.y * 0.5 + 0.5);
    float rim = pow(1.0 - NoV, 3.0) * 0.25;

    vec3 color = direct + ambient + rim * uSkyColor * albedo;
    color = AcesFilm(color * 1.15);
    // No PostProcessChain in this demo, so the linear->display encode happens
    // here. Skipping it is the classic "colours are right but the image is dull"
    // bug, and it is invisible in an isolated shader review.
    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

// The floor: one quad, a procedural grid and a contact shadow. The grid scrolls
// by speed*time because the character walks in place (a treadmill) - the walk
// cycle is then a pure function of the clock, which is what makes --freeze-at
// frames reproducible frame-for-frame.
inline constexpr const char* kGroundVertex = R"GLSL(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uViewProj;

out vec3 vWorldPos;

void main() {
    vWorldPos = aPosition;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
)GLSL";

inline constexpr const char* kGroundFragment = R"GLSL(
#version 410 core

in vec3 vWorldPos;

uniform vec3  uCamPos;
uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform float uSunIntensity;
uniform vec3  uSkyColor;
uniform vec3  uGroundColor;
uniform vec3  uCharPos;         // contact shadow centre (x, z used)
uniform float uScroll;          // metres of floor travelled so far

out vec4 FragColor;

const float PI = 3.14159265359;

vec3 AcesFilm(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// Distance to the nearest grid line, measured in pixels so the line keeps the
// same visual weight at every distance instead of aliasing into a moire.
float GridLine(vec2 p, float spacing, float px) {
    vec2 c = p / spacing;
    vec2 fw = max(fwidth(c), vec2(1e-6));
    vec2 d = abs(fract(c - 0.5) - 0.5) / fw;
    float k = min(d.x, d.y);
    return 1.0 - smoothstep(px * 0.5, px * 0.5 + 1.0, k);
}

void main() {
    vec3 L = normalize(-uSunDir);
    float NoL = max(L.y, 0.0);

    vec2 p = vec2(vWorldPos.x, vWorldPos.z + uScroll);
    float minor = GridLine(p, 0.50, 1.0);
    float major = GridLine(p, 2.50, 1.6);

    vec3 albedo = uGroundColor * (1.0 + 0.30 * minor + 0.55 * major);

    // Contact shadow: an ellipse under the character. Without it the figure
    // reads as floating, because a treadmill never moves its feet out of frame.
    float shadow = 1.0 - smoothstep(0.0, 1.0,
                                    length((vWorldPos.xz - uCharPos.xz) / vec2(0.34, 0.52)));
    albedo *= 1.0 - 0.70 * shadow;

    vec3 direct = uSunColor * uSunIntensity * NoL * albedo / PI;
    vec3 color = direct + albedo * uSkyColor;

    // Fade into the sky so the 80 m quad has no visible edge.
    float dist = length(vWorldPos.xz - uCamPos.xz);
    float fade = smoothstep(7.0, 26.0, dist);
    color = mix(color, uSkyColor * 0.55, fade);

    color = AcesFilm(color * 1.15);
    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
)GLSL";

} // namespace anim_rigid::shaders

#endif // ANIM_RIGID_SHADERS_H
