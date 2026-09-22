#version 410 core
// LOADED at runtime by the `geometry_shader_file` demo
// (src/demo/geometry_shader_file) via gldx::ShaderProgram::CreateFromFiles as
// the VERTEX stage. This is NOT one of the non-loaded reference mirrors in the
// parent directory - it really is read from disk.
//
// Reads one point centre (an NDC position) and forwards it to the geometry
// stage, which expands every point into a square. Kept deliberately tiny so the
// demo is about *loading a multi-stage program from files*, not geometry.
layout(location = 0) in vec2 aPos;
out vec2 vCenter;
void main() {
    vCenter = aPos;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
