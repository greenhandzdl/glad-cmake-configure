#ifndef SHADER_STAGES_STAGES_H
#define SHADER_STAGES_STAGES_H

/**
 * @file Stages.h
 * @brief The five GLSL 4.10 core pipeline stages for the shader_stages demo.
 *
 * Kept out of main.cpp so the entry point stays a thin wiring file: this header
 * is the single source of truth for the shader text, exactly as the geometry /
 * render_passes demos embed their GLSL. Together the five stages form one full
 * graphics pipeline — Vertex -> Tessellation Control -> Tessellation Evaluation
 * -> Geometry -> Fragment — assembled through the single selective entry point
 * gldx::ShaderProgram::CreateFromSources. No compute stage: GL_COMPUTE_SHADER is
 * 4.3+, above this project's 4.1 core baseline.
 *
 * What the program draws is itself the evidence every stage ran: a four-corner
 * patch (GL_PATCHES) is subdivided by the tessellation pair into a dense quad
 * mesh, then the geometry stage re-emits each triangle as a line-strip outline,
 * so the fragment stage only ever colours wireframe edges. Remove the tess pair
 * and the mesh is a single quad; remove the geometry stage and it is a solid
 * fill rather than a lattice. A screenshot full of subdivided wireframe is
 * therefore proof all five stages compiled, attached and linked together.
 */

#include <cstddef>

namespace shader_stages {

// 1. VERTEX: reads one patch control point (an NDC corner) and hands it up the
//    pipe. gl_Position is intentionally left to the tessellation evaluation.
inline constexpr const char* kVertex = R"GLSL(#version 410 core
layout(location = 0) in vec2 aPos;
out vec2 vc;              // control point -> tessellation control
void main() {
    vc = aPos;
}
)GLSL";

// 2. TESSELLATION CONTROL: declares a 4-vertex patch, sets the outer/inner
//    levels (which breathe with the clock so the lattice density animates), and
//    forwards each control point to the evaluation stage.
inline constexpr const char* kTessControl = R"GLSL(#version 410 core
layout(vertices = 4) out;
in vec2 vc[];
out vec2 tc[];
uniform float uPhase;
void main() {
    if (gl_InvocationID == 0) {
        float lvl = 5.0 + 4.0 * (0.5 + 0.5 * sin(uPhase));
        gl_TessLevelOuter[0] = lvl;
        gl_TessLevelOuter[1] = lvl;
        gl_TessLevelOuter[2] = lvl;
        gl_TessLevelOuter[3] = lvl;
        gl_TessLevelInner[0] = lvl;
        gl_TessLevelInner[1] = lvl;
    }
    tc[gl_InvocationID] = vc[gl_InvocationID];
}
)GLSL";

// 3. TESSELLATION EVALUATION: interpolates a point inside the quad from the four
//    control points using gl_TessCoord, gives it a gentle spin and aspect fix,
//    and emits the real gl_Position plus a UV the later stages colour from.
inline constexpr const char* kTessEval = R"GLSL(#version 410 core
layout(quads, equal_spacing, ccw) in;
in vec2 tc[];
out vec2 vUv;
uniform float uPhase, uAspect;
void main() {
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;
    vec2 p = (1.0 - u) * (1.0 - v) * tc[0]
           +         u  * (1.0 - v) * tc[1]
           +         u  *       v   * tc[2]
           + (1.0 - u) *       v   * tc[3];
    float a = 0.4 * sin(uPhase);
    mat2 rot = mat2(cos(a), -sin(a), sin(a), cos(a));
    p = rot * p;
    p.x /= uAspect;
    vUv = gl_TessCoord.xy;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

// 4. GEOMETRY: consumes the triangles the tessellator produced and re-emits each
//    as a closed line strip (its three edges). This is what turns the subdivided
//    surface into a visible wireframe lattice, so you can literally see the GS
//    ran — without it the surface would render as a solid fill.
inline constexpr const char* kGeometry = R"GLSL(#version 410 core
layout(triangles) in;
layout(line_strip, max_vertices = 4) out;
in vec2 vUv[];
out vec2 gUv;
void main() {
    for (int i = 0; i < 3; ++i) {
        gUv = vUv[i];
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    gUv = vUv[0];
    gl_Position = gl_in[0].gl_Position;
    EmitVertex();
    EndPrimitive();
}
)GLSL";

// 5. FRAGMENT: colours the wireframe from the tessellation UV so the lattice
//    reads with a gradient rather than a flat tint.
inline constexpr const char* kFragment = R"GLSL(#version 410 core
in vec2 gUv;
out vec4 FragColor;
void main() {
    vec3 col = mix(vec3(0.20, 0.60, 0.90), vec3(0.95, 0.75, 0.30), gUv.x);
    FragColor = vec4(col, 1.0);
}
)GLSL";

} // namespace shader_stages

#endif // SHADER_STAGES_STAGES_H
