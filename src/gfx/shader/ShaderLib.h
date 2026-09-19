#ifndef GFX_SHADER_SHADERLIB_H
#define GFX_SHADER_SHADERLIB_H

/**
 * @file ShaderLib.h
 * @brief Embedded GLSL (410 core) for the Phase 2 forward renderer.
 *
 * Grouped families:
 *   - PBR:        metallic/roughment forward shader with IBL + cascaded shadows.
 *   - DepthOnly:  shadow-caster pass (writes only gl_FragDepth via fixed function).
 *   - Skybox:     samples the source environment cube as the background.
 *
 * Uniform-buffer binding points are assigned from C++ (GLSL 4.10 has no
 * layout(binding=...) on uniform blocks): ShaderProgram::SetBlockBinding maps
 *   LightingBlock -> 1 (gfx::LightBuffer::kBinding)
 *   ShadowBlock   -> 2 (gfx::CascadedShadowMap::kShadowBinding)
 * Texture unit conventions are documented on PbrMaterial.
 */

#include <glad/gl.h>

namespace gfx::shaders {

// ---------------------------------------------------------------------------
// PBR
// ---------------------------------------------------------------------------
inline const char* kPbrVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aTangent;

uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal   = uNormalMatrix * aNormal;
    vTangent  = uNormalMatrix * aTangent;
    vUV       = aUV;
    gl_Position = uViewProj * wp;
}
)GLSL";

inline const char* kPbrFragment = R"GLSL(#version 410 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
out vec4 FragColor;

// Point-light struct (declared at global scope: GLSL forbids type definitions
// inside a uniform block). std140 lays two vec4 members out as 32 B/element,
// matching gfx::PointLightGpu's interleaved array in LightingBlockGpu.
struct PL { vec4 posRange; vec4 colorIntensity; };

layout(std140) uniform LightingBlock {
    vec4  cameraPos;
    vec4  dirColor;       // sun rgb, intensity in w
    vec4  dirDirection;   // sun travel direction xyz
    vec4  ambientTint;
    ivec4 pointCount;
    PL points[32];
};
layout(std140) uniform ShadowBlock {
    mat4 lightMat[4];
    vec4 cascadeSplits;
    vec4 shadowParams;    // x = cascade count
};

uniform sampler2D uAlbedo;
uniform sampler2D uMetalRough;   // G = roughness, B = metallic
uniform sampler2D uAo;
uniform sampler2D uNormal;       // tangent-space, sRGB off
uniform sampler2DArrayShadow uShadowMap;
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D   uBrdfLut;

uniform int   uHasAlbedo;
uniform int   uHasMetalRough;
uniform int   uHasNormal;
uniform int   uHasAo;
uniform int   uUseShadow;
uniform int   uUseIbl;
uniform vec4  uBaseColor;        // rgba fallback
uniform float uMetallic;
uniform float uRoughness;
uniform float uNormalScale;
uniform float uAlphaCutoff;

const float PI = 3.14159265359;

vec3 GetNormal() {
    vec3 n = normalize(vNormal);
    if (uHasNormal == 1) {
        vec3 t = normalize(vTangent);
        t = normalize(t - dot(t, n) * n);
        vec3 b = cross(n, t);
        mat3 tbn = mat3(t, b, n);
        vec3 mapped = texture(uNormal, vUV).rgb * 2.0 - 1.0;
        mapped.xy *= uNormalScale;
        n = normalize(tbn * mapped);
    }
    return n;
}

float DistributionGGX(vec3 N, vec3 H, float rough) {
    float a = rough * rough;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * d * d + 1e-7);
}
float GeometrySchlickGGX(float NdotV, float rough) {
    float r = (rough + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, rough) * GeometrySchlickGGX(NdotL, rough);
}
vec3 FresnelSchlick(float cosT, vec3 F0) {
    cosT = clamp(cosT, 0.0, 1.0);
    return F0 + (1.0 - F0) * pow(1.0 - cosT, 5.0);
}
vec3 FresnelSchlickRoughness(float cosT, vec3 F0, float rough) {
    cosT = clamp(cosT, 0.0, 1.0);
    return F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(1.0 - cosT, 5.0);
}

// Choose cascade by view-space depth vs split distances.
float CascadeLayer(float viewDepth) {
    if (viewDepth < cascadeSplits.x) return 0.0;
    if (viewDepth < cascadeSplits.y) return 1.0;
    if (viewDepth < cascadeSplits.z) return 2.0;
    return 3.0;
}

float ShadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    if (uUseShadow != 1) return 1.0;
    float viewDepth = length(worldPos - cameraPos.xyz);
    float layer = CascadeLayer(viewDepth);
    vec4 lp = lightMat[int(layer)] * vec4(worldPos + N * 0.002, 1.0);
    vec3 proj = lp.xyz / lp.w * 0.5 + 0.5;
    if (proj.z > 1.0) return 1.0;
    // 3x3 PCF: for sampler2DArrayShadow texture() takes vec4(s, t, layer, ref).
    ivec3 sz = textureSize(uShadowMap, 0);
    vec2 texel = 1.0 / vec2(sz.xy);
    float bias = max(0.0035 * (1.0 - dot(N, L)), 0.0012);
    float s = 0.0;
    for (int x = -1; x <= 1; ++x)
      for (int y = -1; y <= 1; ++y)
        s += texture(uShadowMap, vec4(proj.xy + vec2(x, y) * texel * 1.5, layer, proj.z - bias));
    return s / 9.0;
}

void main() {
    vec3 albedo = uBaseColor.rgb;
    float alpha = uBaseColor.a;
    if (uHasAlbedo == 1) {
        vec4 t = texture(uAlbedo, vUV);
        albedo *= t.rgb;
        alpha *= t.a;
    }
    if (alpha < uAlphaCutoff) discard;

    float metallic  = uMetallic;
    float roughness = uRoughness;
    if (uHasMetalRough == 1) {
        vec2 mr = texture(uMetalRough, vUV).gb;
        metallic  = mr.y;
        roughness = mr.x;
    }
    roughness = clamp(roughness, 0.035, 1.0);
    float ao = (uHasAo == 1) ? texture(uAo, vUV).r : 1.0;

    vec3 N = GetNormal();
    vec3 V = normalize(cameraPos.xyz - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);

    // Directional (sun).
    {
        vec3 L = normalize(-dirDirection.xyz);
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        vec3 radiance = dirColor.rgb * dirColor.w;
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 spec = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL + 1e-4, 1e-4);
        vec3 kD = (1.0 - F) * (1.0 - metallic);
        Lo += (kD * albedo / PI + spec) * radiance * NdotL * ShadowFactor(vWorldPos, N, L);
    }

    // Point lights.
    for (int i = 0; i < pointCount.x; ++i) {
        vec3 p = points[i].posRange.xyz;
        float range = points[i].posRange.w;
        vec3 color = points[i].colorIntensity.rgb * points[i].colorIntensity.w;
        vec3 d = p - vWorldPos;
        float dist = length(d);
        vec3 L = d / max(dist, 1e-4);
        float atten = clamp(1.0 - (dist / range), 0.0, 1.0);
        atten *= atten;
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        float D = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 spec = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL + 1e-4, 1e-4);
        vec3 kD = (1.0 - F) * (1.0 - metallic);
        Lo += (kD * albedo / PI + spec) * color * atten * NdotL;
    }

    // Image-based ambient + specular.
    vec3 ambient = ambientTint.rgb * albedo * ao;
    if (uUseIbl == 1) {
        vec3 kS = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kD = (1.0 - kS) * (1.0 - metallic);
        vec3 irradiance = texture(uIrradiance, N).rgb;
        vec3 diffuse = irradiance * albedo;
        float maxMip = float(textureQueryLod(uPrefilter, N).y);
        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(uPrefilter, R, roughness * maxMip).rgb;
        vec2 brdf = texture(uBrdfLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
        vec3 specular = prefiltered * (F0 * brdf.x + brdf.y);
        ambient = kD * diffuse + specular;
        ambient *= ao;
    }

    vec3 color = ambient + Lo;
    // Linear HDR output; exposure + ACES tone map + gamma live in the Phase 3
    // post-process composite (see PostProcessShaders.h / PostProcessChain).
    FragColor = vec4(color, alpha);
}
)GLSL";

// ---------------------------------------------------------------------------
// Depth-only (shadow casters)
// ---------------------------------------------------------------------------
inline const char* kDepthVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightMat;
void main() { gl_Position = uLightMat * uModel * vec4(aPos, 1.0); }
)GLSL";

inline const char* kDepthFragment = R"GLSL(#version 410 core
void main() { }
)GLSL";

// ---------------------------------------------------------------------------
// Skybox background
// ---------------------------------------------------------------------------
inline const char* kSkyboxVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj;
out vec3 vDir;
void main() {
    vDir = aPos;
    vec4 p = uViewProj * vec4(aPos, 1.0);
    gl_Position = p.xyww;   // force max depth
}
)GLSL";

inline const char* kSkyboxFragment = R"GLSL(#version 410 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uEnv;
void main() {
    // Linear HDR sky; the post composite tone-maps it like the rest of the
    // scene so the sun disc blooms consistently with emissive geometry.
    vec3 c = texture(uEnv, normalize(vDir)).rgb;
    FragColor = vec4(c, 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// Instanced (Phase 4): per-instance model matrix + colour, lit by the sun from
// the shared LightingBlock UBO. No textures are sampled, so it sidesteps the
// sampler-type / placeholder concerns entirely and stays a cheap perf demo.
// ---------------------------------------------------------------------------
inline const char* kInstancedVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=4) in mat4 aInstance;   // consumes locations 4,5,6,7
layout(location=8) in vec4 aColor;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vColor;

void main() {
    vec4 wp = aInstance * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    // Rigid instance transform (rotation + uniform scale): the normal matrix is
    // just the upper 3x3 with the scale factored out by normalise(); GLSL 4.10
    // has no inverse(), so we avoid it deliberately.
    vNormal   = normalize(mat3(aInstance) * aNormal);
    vColor    = aColor;
    gl_Position = uViewProj * wp;
}
)GLSL";

inline const char* kInstancedFragment = R"GLSL(#version 410 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vColor;
out vec4 FragColor;

struct PL { vec4 posRange; vec4 colorIntensity; };
layout(std140) uniform LightingBlock {
    vec4  cameraPos;
    vec4  dirColor;       // sun rgb, intensity in w
    vec4  dirDirection;   // sun travel direction xyz
    vec4  ambientTint;
    ivec4 pointCount;
    PL points[32];
};

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-dirDirection.xyz);           // toward the sun
    vec3 V = normalize(cameraPos.xyz - vWorldPos);
    float ndl = max(dot(N, L), 0.0);
    vec3  H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * ndl;

    vec3 sun = dirColor.rgb * dirColor.w;
    vec3 color = vColor.rgb * (ambientTint.xyz + sun * ndl) + sun * spec;
    FragColor = vec4(color, 1.0);                     // linear HDR; composite tone-maps
}
)GLSL";

} // namespace gfx::shaders

#endif // GFX_SHADER_SHADERLIB_H
