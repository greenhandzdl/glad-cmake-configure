// -----------------------------------------------------------------------------
// REFERENCE MIRROR - not compiled by the build.
// The runtime embeds these programs as raw strings in
// src/gfx/shader/DebugShaders.h (kDebugVertex / kDebugFragment); that header is
// the single source of truth. This file is a browsable/editable copy only.
//
// Minimal GLSL for the DebugDraw line overlay. Per-vertex position + RGBA
// colour through a single view-projection uniform; drawn as GL_LINES on top of
// the tone-mapped frame (depth test disabled), so helper geometry is always
// visible regardless of scene depth.
// -----------------------------------------------------------------------------

// ===== debug.vert =====
#version 410 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aColor;
uniform mat4 uViewProj;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}

// ===== debug.frag =====
#version 410 core
in vec4 vColor;
out vec4 FragColor;
void main() {
    FragColor = vColor;
}
