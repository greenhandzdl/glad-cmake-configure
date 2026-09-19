#ifndef GFX_SHADER_SPRITESHADERS_H
#define GFX_SHADER_SPRITESHADERS_H

/**
 * @file SpriteShaders.h
 * @brief GLSL (410 core) for the 2D SpriteBatch used by the HUD overlay.
 *
 * Sprites are emitted as screen-space quads (pixels, origin top-left) and
 * projected by an orthographic matrix supplied by SpriteBatch::Begin. Colour is
 * per-vertex so text/tints modulate a single atlas. Output is written directly
 * to the (already tone-mapped) default framebuffer, so no gamma is applied.
 */

namespace gfx::shaders {

inline const char* kSpriteVertex = R"GLSL(#version 410 core
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
)GLSL";

inline const char* kSpriteFragment = R"GLSL(#version 410 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;

uniform sampler2D uTex;

void main() {
    FragColor = vColor * texture(uTex, vUV);
}
)GLSL";

} // namespace gfx::shaders

#endif // GFX_SHADER_SPRITESHADERS_H
