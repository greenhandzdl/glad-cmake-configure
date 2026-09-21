// -----------------------------------------------------------------------------
// REFERENCE MIRROR - not compiled by the build.
// The runtime embeds these programs as raw strings in src/gldx/shader/ShaderLib.h
// (kSkyboxVertex / kSkyboxFragment); that header is the single source of truth.
// This file is a browsable/editable copy only.
// -----------------------------------------------------------------------------

// ===== skybox.vert =====
#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uViewProj;
out vec3 vDir;
void main() {
    vDir = aPos;
    vec4 p = uViewProj * vec4(aPos, 1.0);
    gl_Position = p.xyww;   // force max depth
}

// ===== skybox.frag =====
#version 410 core
in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uEnv;
void main() {
    // Linear HDR sky; the post composite tone-maps it like the rest of the
    // scene so the sun disc blooms consistently with emissive geometry.
    vec3 c = texture(uEnv, normalize(vDir)).rgb;
    FragColor = vec4(c, 1.0);
}
