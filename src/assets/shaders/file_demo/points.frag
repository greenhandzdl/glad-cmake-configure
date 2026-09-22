#version 410 core
// LOADED at runtime by the `geometry_shader_file` demo via
// gldx::ShaderProgram::CreateFromFiles as the FRAGMENT stage. Shades each square
// emitted by the geometry stage with a radial falloff from its local coordinate,
// so the file-loaded geometry -> fragment interface (vLocal) is visibly wired.
in vec2 vLocal;
out vec4 FragColor;
void main() {
    float d = clamp(1.0 - length(vLocal), 0.0, 1.0);
    vec3 base = vec3(0.18, 0.45, 0.75);
    FragColor = vec4(base + 0.45 * d, 1.0);
}
