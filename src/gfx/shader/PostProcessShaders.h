#ifndef GFX_SHADER_POSTPROCESSSHADERS_H
#define GFX_SHADER_POSTPROCESSSHADERS_H

/**
 * @file PostProcessShaders.h
 * @brief Embedded GLSL (410 core) for the Phase 3 post-process chain.
 *
 * The geometry pass now renders linear HDR into an offscreen RGBA16F target
 * (MSAA-resolved), so tone mapping + gamma live here rather than in the PBR
 * shader. Passes:
 *   kBrightPass  -> extract pixels above a luminance threshold (bloom source)
 *   kBlur        -> separable gaussian blur (run horizontally then vertically)
 *   kComposite   -> scene + bloom, exposure, ACES filmic tone map, gamma
 *
 * All passes draw a single fullscreen triangle generated from gl_VertexID in
 * kPostVertex, so no vertex buffer is required.
 */

namespace gfx::shaders {

inline const char* kPostVertex = R"GLSL(#version 410 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

inline const char* kBrightPassFragment = R"GLSL(#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uImage;
uniform float uThreshold;   // linear luminance cut-off
uniform float uSoftKnee;

void main() {
    vec3 c = texture(uImage, vUV).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float knee = max(uSoftKnee, 1e-4);
    float weight = clamp((lum - uThreshold) / knee, 0.0, 1.0);
    weight *= weight;
    FragColor = vec4(c * weight, 1.0);
}
)GLSL";

inline const char* kBlurFragment = R"GLSL(#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uImage;
uniform vec2 uDirection;   // already scaled to texel size (and optional spread)

void main() {
    // 9-tap gaussian (sigma ~ 2): weights sum to 1.
    float w[5];
    w[0] = 0.227027; w[1] = 0.1945946; w[2] = 0.1216216;
    w[3] = 0.0540540; w[4] = 0.0162162;

    vec3 result = texture(uImage, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 off = uDirection * float(i);
        result += texture(uImage, vUV + off).rgb * w[i];
        result += texture(uImage, vUV - off).rgb * w[i];
    }
    FragColor = vec4(result, 1.0);
}
)GLSL";

inline const char* kCompositeFragment = R"GLSL(#version 410 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uScene;   // linear HDR (post-MSAA)
uniform sampler2D uBloom;   // blurred bright pass, linear HDR
uniform float     uBloomStrength;
uniform float     uExposure;

vec3 ACESFilm(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 color = texture(uScene, vUV).rgb;
    color += texture(uBloom, vUV).rgb * uBloomStrength;
    color *= uExposure;
    color = ACESFilm(color);
    color = pow(color, vec3(1.0 / 2.2));
    FragColor = vec4(color, 1.0);
}
)GLSL";

} // namespace gfx::shaders

#endif // GFX_SHADER_POSTPROCESSSHADERS_H
