#version 410 core
// LOADED at runtime by the `shader_file` demo (src/demo/shader_file) via
// gldx::ShaderProgram::CreateFromFiles. This is NOT one of the non-loaded
// reference mirrors in the parent directory - it really is read from disk.
//
// A full-screen triangle generated from gl_VertexID alone: gl_VertexID 0/1/2
// maps to clip positions (-1,-1)/(3,-1)/(-1,3), covering the viewport without
// any vertex buffer. That keeps the demo about *loading a program from files*,
// not about geometry plumbing.
out vec2 vUV;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
