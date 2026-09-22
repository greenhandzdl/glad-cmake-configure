#version 410 core
// LOADED at runtime by the `geometry_shader_file` demo via
// gldx::ShaderProgram::CreateFromFiles as the GEOMETRY stage. This is the whole
// point of the demo: a geometry shader assembled *from a file*, something the
// two-path CreateFromFiles(vert, frag) cannot express. If this stage did not
// compile and link from disk, the point cloud below would render as bare dots
// (or nothing), never as the squares you see - so a snapshot full of squares is
// direct proof the multi-stage file loader really ran.
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;
in vec2 vCenter[];
out vec2 vLocal;               // -1..1 within the square, for shading
uniform float uHalf;           // half-size in NDC
void main() {
    vec2 corners[4] = vec2[4](
        vec2(-1.0, -1.0), vec2(1.0, -1.0),
        vec2(-1.0,  1.0), vec2(1.0,  1.0));
    for (int i = 0; i < 4; ++i) {
        vLocal = corners[i];
        gl_Position = vec4(vCenter[0] + corners[i] * uHalf, 0.0, 1.0);
        EmitVertex();
    }
    EndPrimitive();
}
