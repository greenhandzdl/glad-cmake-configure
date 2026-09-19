// -----------------------------------------------------------------------------
// REFERENCE MIRROR - not compiled by the build.
// The runtime embeds these programs as raw strings in src/gfx/shader/ShaderLib.h
// (kDepthVertex / kDepthFragment); that header is the single source of truth.
// This file is a browsable/editable copy only.
// -----------------------------------------------------------------------------

// ===== depth.vert =====
#version 410 core
layout(location=0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightMat;
void main() { gl_Position = uLightMat * uModel * vec4(aPos, 1.0); }

// ===== depth.frag =====
#version 410 core
void main() { }
