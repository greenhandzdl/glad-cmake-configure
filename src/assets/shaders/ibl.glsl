// -----------------------------------------------------------------------------
// REFERENCE MIRROR - not compiled by the build.
// The runtime embeds these programs as raw strings in src/gldx/shader/IblShaders.h
// (kIblVertex / kSkyGenFragment / kIrradianceFragment / kPrefilterFragment /
// kBrdfFragment); that header is the single source of truth. This file is a
// browsable/editable copy only.
//
// Image-based-lighting precomputation. All passes render a fullscreen quad into
// one cube face at a time; the face orientation is supplied per-draw as three
// orthonormal basis uniforms (uRight/uUp/uFacing), so no view/projection
// matrices are used for the environment passes.
// -----------------------------------------------------------------------------

// ===== ibl.vert (fullscreen triangle; emits uv + world direction) =====
#version 410 core
out vec2 vUV;
// Fullscreen triangle from gl_VertexID (0..2); no vertex buffer needed.
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}

// ===== skygen.frag (procedural HDR sky -> source cube) =====
#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uFacing;
uniform vec3 uSunDir;      // travel direction (normalized)

const float PI = 3.14159265359;

vec3 sky(vec3 d) {
    float h = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.62, 0.68, 0.78);
    vec3 zenith  = vec3(0.18, 0.32, 0.62);
    vec3 ground  = vec3(0.22, 0.22, 0.24);
    vec3 col = mix(horizon, zenith, smoothstep(0.5, 1.0, h));
    col = mix(col, ground, smoothstep(0.5, 0.35, h));
    // Sun disc along the (negative) travel direction.
    vec3 toSun = normalize(-uSunDir);
    float sd = max(dot(d, toSun), 0.0);
    col += vec3(12.0, 9.5, 6.5) * pow(sd, 700.0);   // bright core
    col += vec3(2.0, 1.7, 1.3) * pow(sd, 24.0) * 0.4;
    return max(col, vec3(0.0));
}

void main() {
    vec2 c = vUV * 2.0 - 1.0;
    vec3 dir = normalize(c.x * uRight + c.y * uUp + uFacing);
    FragColor = vec4(sky(dir), 1.0);
}

// ===== irradiance.frag (cosine-weighted hemisphere convolution) =====
#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform samplerCube uEnv;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uFacing;

const float PI = 3.14159265359;

void main() {
    vec2 c = vUV * 2.0 - 1.0;
    vec3 N = normalize(c.x * uRight + c.y * uUp + uFacing);

    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sum = vec3(0.0);
    float samples = 0.0;
    for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 16; ++j) {
            float u1 = (float(i) + 0.5) / 24.0;
            float u2 = (float(j) + 0.5) / 16.0;
            float r = sqrt(u1);
            float phi = 2.0 * PI * u2;
            vec3 h = vec3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u1)));
            vec3 L = tangent * h.x + bitangent * h.y + N * h.z;
            sum += texture(uEnv, normalize(L)).rgb * max(dot(N, L), 0.0);
            samples += 1.0;
        }
    }
    FragColor = vec4(sum / samples, 1.0);
}

// ===== prefilter.frag (GGX importance-sampled specular at one roughness/mip) =====
#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform samplerCube uEnv;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uFacing;
uniform float uRoughness;

const float PI = 3.14159265359;

float VNG(uint seed) {
    uint i = seed * 1973u + 9277u;
    i = (i << 13u) ^ i;
    i = (i >> 16u) ^ i;
    return float(i & 0xFFFFu) / 65535.0;
}

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * d * d);
}

vec3 ImportanceSampleGGX(vec2 e, vec3 N) {
    float a = uRoughness * uRoughness;
    float phi = 2.0 * PI * e.x;
    float cosT = sqrt((1.0 - e.y) / (1.0 + (a * a - 1.0) * e.y));
    float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
    vec3 H = vec3(sinT * cos(phi), sinT * sin(phi), cosT);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return tangent * H.x + bitangent * H.y + N * H.z;
}

void main() {
    vec2 c = vUV * 2.0 - 1.0;
    vec3 N = normalize(c.x * uRight + c.y * uUp + uFacing);
    vec3 R = N;
    vec3 V = R;

    const uint SAMPLES = 256u;
    vec3 pre = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < SAMPLES; ++i) {
        vec2 e = vec2(VNG(i), VNG(i + uint(SAMPLES)));
        vec3 L = ImportanceSampleGGX(e, N);
        vec3 H = L;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            pre += texture(uEnv, L).rgb * NdotL;
            weight += NdotL;
        }
    }
    FragColor = vec4(pre / max(weight, 1e-4), 1.0);
}

// ===== brdf.frag (Karis split-sum BRDF integration LUT, 2D) =====
#version 410 core
in vec2 vUV;
out vec4 FragColor;

const float PI = 3.14159265359;

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float d = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * d * d);
}
float GeometrySchlickGGX(float NdotV, float rough) {
    float r = (rough + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(float NdotV, float NdotL, float rough) {
    return GeometrySchlickGGX(NdotL, rough) * GeometrySchlickGGX(NdotV, rough);
}
vec2 ImportancesampleGGX(vec2 e, float rough) {
    float a = rough * rough;
    float phi = 2.0 * PI * e.x;
    float cosT = sqrt((1.0 - e.y) / (1.0 + (a * a - 1.0) * e.y));
    float sinT = sqrt(max(0.0, 1.0 - cosT * cosT));
    return vec2(sinT * cos(phi), cosT);
}

void main() {
    float NdotV = max(vUV.x, 1e-4);
    float rough = max(vUV.y, 0.01);
    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    vec3 N = vec3(0.0, 0.0, 1.0);

    float A = 0.0, B = 0.0;
    const uint SAMPLES = 512u;
    for (uint i = 0u; i < SAMPLES; ++i) {
        vec2 e = vec2((float(i) + 0.5) / float(SAMPLES),
                      (fract(float(i) * 0.61803398875)));
        vec2 Hs = ImportancesampleGGX(e, rough);
        vec3 H = vec3(Hs.x, 0.0, Hs.y);
        vec3 L = reflect(-V, H);
        float NdotL = L.z;
        if (NdotL > 0.0) {
            float G = GeometrySmith(NdotV, NdotL, rough);
            float GVis = G * NdotL / (max(NdotV * NdotL, 1e-4) * 4.0);
            float Fc = pow(1.0 - max(dot(V, H), 0.0), 5.0);
            A += (1.0 - Fc) * GVis;
            B += Fc * GVis;
        }
    }
    FragColor = vec4(A / float(SAMPLES), B / float(SAMPLES), 0.0, 1.0);
}
