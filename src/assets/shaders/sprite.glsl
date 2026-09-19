// -----------------------------------------------------------------------------
// REFERENCE MIRROR - not compiled by the build.
// The runtime embeds these programs as raw strings in
// src/gfx/shader/SpriteShaders.h (kSpriteVertex / kSpriteFragment); that header
// is the single source of truth. This file is a browsable/editable copy only.
//
// 2D SpriteBatch used by the HUD overlay. Sprites are emitted as screen-space
// quads (pixels, origin top-left) and projected by an orthographic matrix
// supplied by SpriteBatch::Begin. Colour is per-vertex so text/tints modulate a
// single atlas. Output is written directly to the (already tone-mapped) default
// framebuffer, so no gamma is applied.
// -----------------------------------------------------------------------------

// ===== sprite.vert =====
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uProj;

out vec2 vUV;
out vec4 vColor;

void main() {
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}

// ===== sprite.frag =====
#version 410 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;

uniform sampler2D uTex;

void main() {
    FragColor = vColor * texture(uTex, vUV);
}
