#version 410 core
// Companion to triangle.vert; loaded by the shader_file demo (see that file's
// header). Kept identical in effect to the demo's embedded fallback source, so
// a "loaded from files" run and a "no --vert/--frag" run look the same.
in vec2 vUV;
out vec4 FragColor;
void main() {
    // Warm diagonal gradient across the screen. No clear colour could fake this,
    // so a colourful, non-empty snapshot is proof the file-loaded program
    // compiled, linked and actually drew.
    float t = clamp(vUV.x + vUV.y, 0.0, 1.0);
    vec3 c = mix(vec3(0.10, 0.40, 0.80), vec3(0.90, 0.50, 0.20), t);
    FragColor = vec4(c, 1.0);
}
