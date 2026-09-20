// -----------------------------------------------------------------------------
// REFERENCE MIRROR - not compiled by the build.
// The runtime embeds these programs as raw strings in src/gfx/shader/ShaderLib.h
// (kInstancedVertex / kInstancedFragment); that header is the single source of
// truth. This file is a browsable/editable copy only.
//
// Instanced (Phase 4): per-instance model matrix + colour, lit by the sun from
// the shared LightingBlock UBO. No textures are sampled, so it sidesteps the
// sampler-type / placeholder concerns entirely and stays a cheap perf demo.
// -----------------------------------------------------------------------------

// ===== instanced.vert =====
#version 410 core
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

// ===== instanced.frag =====
#version 410 core
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
    vec4  fogColor;       // linear HDR rgb + pad
    vec4  fogParams;      // x enabled, y start, z end
};

vec3 ApplyFog(vec3 color, vec3 worldPos) {
    if (fogParams.x < 0.5) return color;
    float dist = length(cameraPos.xyz - worldPos);
    float t = clamp((dist - fogParams.y) / max(fogParams.z - fogParams.y, 1e-4), 0.0, 1.0);
    return mix(color, fogColor.rgb, t);
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-dirDirection.xyz);           // toward the sun
    vec3 V = normalize(cameraPos.xyz - vWorldPos);
    float ndl = max(dot(N, L), 0.0);
    vec3  H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * ndl;

    vec3 sun = dirColor.rgb * dirColor.w;
    vec3 color = vColor.rgb * (ambientTint.xyz + sun * ndl) + sun * spec;
    color = ApplyFog(color, vWorldPos);
    FragColor = vec4(color, 1.0);                     // linear HDR; composite tone-maps
}
