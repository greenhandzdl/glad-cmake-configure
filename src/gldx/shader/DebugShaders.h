#ifndef GLDX_SHADER_DEBUGSHADERS_H
#define GLDX_SHADER_DEBUGSHADERS_H

/**
 * @file DebugShaders.h
 * @brief Minimal GLSL (410 core) for the DebugDraw line overlay.
 *
 * Per-vertex position + RGBA colour through a single view-projection uniform;
 * drawn as GL_LINES on top of the tone-mapped frame (depth test disabled), so
 * helper geometry is always visible regardless of scene depth.
 */

namespace gldx::shaders {

inline const char* kDebugVertex = R"GLSL(#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)GLSL";

inline const char* kDebugFragment = R"GLSL(#version 410 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
)GLSL";

} // namespace gldx::shaders

#endif // GLDX_SHADER_DEBUGSHADERS_H
