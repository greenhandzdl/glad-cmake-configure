#ifndef GLASS_WATER_SHADERS_H
#define GLASS_WATER_SHADERS_H

/**
 * @file Shaders.h
 * @brief The one GLSL 410 program that every object in the glass_water scene is
 *        drawn with, including the sky.
 *
 * One program is the point. "How transparent is this thing" is not a different
 * shader, it is a handful of numbers on a dielectric BRDF: an index of
 * refraction, a roughness, and how thick the light path through it is. Every
 * item in Scene.cpp therefore differs only in its `Surface` values, and the
 * *behavioural* differences (blend function, depth writes, cull mode, draw
 * order) stay where they belong - in the C++ state machine in BlendPass.cpp.
 * Splitting those two things apart is the whole lesson: the fragment stage
 * decides how much light a pixel mirrors, absorbs and lets through, the pipeline
 * state decides whether it is allowed to overwrite the pixels behind it.
 *
 * The shading model is Cook-Torrance (GGX NDF, Smith height-correlated
 * visibility, Schlick Fresnel) with a dielectric F0 derived from the IOR, plus a
 * two-band analytic sky standing in for an irradiance map. Water is the reason
 * that model is here rather than a Blinn-Phong with a hand-tuned alpha:
 *
 *   - Fresnel is *derived*, not painted. Water at IOR 1.333 reflects 2.0 %
 *     square-on and glass at 1.52 reflects 4.0 % - and both go to 100 % grazing,
 *     which is why a lake is a mirror at the far shore and glass under your feet.
 *     A `pow(1 - NoV, hand-picked exponent)` gets the shape right and the
 *     magnitude wrong, and the magnitude is the one number that has to be right
 *     for the alpha to mean anything.
 *   - The alpha falls out of conservation instead of being authored. A layer
 *     mirrors F, absorbs (1 - T) of what passes, and lets (1 - F) * T of the
 *     background through, so coverage = 1 - (1 - F) * T and the rgb is what the
 *     layer emits. Both terms add to 1 by construction: the surface cannot
 *     invent light at composite time. (That rgb then has to be *scaled* by the
 *     coverage somewhere before it lands - here or in the blend unit, never both
 *     and never neither, or "attenuating the background" and "adding a layer"
 *     become the same event happening twice and the frame goes white; see the
 *     last lines of the fragment stage.)
 *   - Absorption is Beer-Lambert over a *slant* path, so looking straight down
 *     through the basin shows the checkerboard on the bottom while the same
 *     sheet, seen at a shallow angle, is a green wall. That difference is the
 *     physical reason the far half of a lake always looks darker than the near
 *     half, and no texture is involved in producing it.
 *
 * Two things the fragment stage deliberately does NOT do:
 *
 *   - It does not gamma-encode. The scene renders into the chain's linear HDR
 *     target (see PostProcessChain), so alpha blending adds and attenuates
 *     *linear* radiance and the composite stage does exposure + ACES + the final
 *     encode. Blend over an already-encoded value instead - which is what a
 *     pass that draws straight to the window has to do - and every stacked layer
 *     comes out too bright, because (0.5 + 0.5)/2 of two sRGB halves is not the
 *     sRGB of their linear mean. That is also why an additive glow can only
 *     actually glow in a float target: 1.0 + 1.0 in an 8-bit window is 1.0.
 *   - It does not use a normal matrix that the shader has to invent. Non-uniform
 *     scaling is all over this scene (a pane is 2.6 x 3.2 x 0.06 of one unit
 *     cube), and `mat3(uModel) * normal` under a non-uniform scale points the
 *     normal at the wrong surface - for a reflective pane that error is the
 *     entire image. The CPU hands over the inverse-transpose instead.
 */

namespace glass_water {

constexpr const char* kVertex = R"GLSL(#version 410 core

layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;

uniform mat4  uModel;
uniform mat4  uViewProj;
uniform mat3  uNormalMatrix;   // inverse-transpose of uModel, computed on the CPU
uniform float uTime;
uniform float uWaveAmp;        // 0 for everything that is not the water sheet

out vec3 vNormal;
out vec2 vUv;
out vec3 vWorldPos;

// A sum of three crossed sines stands in for a displacement field. The slope is
// the analytic derivative of the *same* expression: taking it from neighbouring
// vertices would need a denser grid than the one the demo uploads, and doing it
// per fragment would mean re-evaluating the field in the fragment stage for a
// benefit (sharp crests at the silhouettes) this demo has no use for.
float WaterHeight(vec2 xz, float t) {
    return 0.65 * sin(xz.x * 0.75 + t * 1.30) * cos(xz.y * 0.62 - t * 0.90)
         + 0.22 * sin((xz.x + xz.y) * 1.70 - t * 2.10)
         + 0.13 * cos((xz.x - xz.y) * 2.60 + t * 1.70);
}

vec2 WaterSlope(vec2 xz, float t) {
    float sx = sin(xz.x * 0.75 + t * 1.30);
    float cx = cos(xz.x * 0.75 + t * 1.30);
    float sy = sin(xz.y * 0.62 - t * 0.90);
    float cy = cos(xz.y * 0.62 - t * 0.90);
    float dx = 0.75 * 0.65 * cx * cy + 1.70 * 0.22 * cos((xz.x + xz.y) * 1.70 - t * 2.10)
             - 2.60 * 0.13 * sin((xz.x - xz.y) * 2.60 + t * 1.70);
    float dy = -0.62 * 0.65 * sx * sy + 1.70 * 0.22 * cos((xz.x + xz.y) * 1.70 - t * 2.10)
             + 2.60 * 0.13 * sin((xz.x - xz.y) * 2.60 + t * 1.70);
    return vec2(dx, dy);
}

void main() {
    vec3 p = aPosition;
    vec3 n = aNormal;
    if (uWaveAmp > 0.0) {
        p.y += uWaveAmp * WaterHeight(p.xz, uTime);
        // A height field y = h(x,z) has the surface normal (-dh/dx, 1, -dh/dz).
        n = normalize(vec3(-uWaveAmp * WaterSlope(p.xz, uTime).x, 1.0,
                           -uWaveAmp * WaterSlope(p.xz, uTime).y));
    }
    vec4 world = uModel * vec4(p, 1.0);
    vWorldPos = world.xyz;
    vNormal   = uNormalMatrix * n;
    vUv       = aUv;
    // The projection is *only* what changes when the camera switches between
    // perspective and orthographic: same vertices, same model matrix, same wave
    // field - uViewProj is either glm::perspective or glm::ortho. Everything that
    // has to know which one it is lives in the fragment stage, because the thing
    // a flat projection actually breaks is the viewing direction, not the
    // geometry (see uOrthographic below).
    gl_Position = uViewProj * world;
}
)GLSL";

constexpr const char* kFragment = R"GLSL(#version 410 core

in vec3 vNormal;
in vec2 vUv;
in vec3 vWorldPos;

uniform vec3  uEye;
uniform vec3  uCamForward;      // the direction an orthographic camera looks along
uniform float uOrthographic;    // 1 -> use it instead of (uEye - vWorldPos)
uniform vec3  uSunDir;          // unit, pointing toward the light
uniform vec3  uSunColor;        // HDR radiance of the sun disk
uniform vec3  uAlbedo;          // linear tint, multiplied into the map's rgb
uniform vec3  uSkyZenith;       // The three bands the dome's map is painted from,
uniform vec3  uSkyHorizon;      // so a reflection can never disagree with the sky
uniform vec3  uSkyNadir;        // it is supposedly reflecting.
uniform float uLightMix;        // 1 = lit, 0 = the sky backdrop (flat map colour)
uniform float uIor;             // dielectric F0 = ((ior-1)/(ior+1))^2
uniform float uRoughness;       // GGX alpha
uniform vec3  uAbsorption;      // Beer-Lambert extinction per world unit
uniform float uPathLen;         // how thick the body the light crosses is
uniform float uBaseAlpha;       // opacity floor: the transparency ladder
uniform float uFresnelToAlpha;  // 1 -> coverage is the conserved amount exactly
uniform float uCoreToAlpha;     // face-on -> opaque (an additive glow ball)
uniform float uAlphaCutoff;     // discard below this map alpha (cutout geometry)
uniform float uIridescence;
uniform float uEmissive;        // HDR multiplier; >1 is what bloom keys on
uniform float uPremultiply;     // 1 -> rgb *= a, the pair (ONE, 1-SRC_ALPHA) wants
uniform vec2  uUvScale;
uniform sampler2D uTex;

out vec4 FragColor;

const float PI = 3.141592653589793;
const vec3  THIRD = vec3(0.33333333);

float F0FromIor(float ior) {
    float f = (ior - 1.0) / (ior + 1.0);
    return f * f;
}

// Trowbridge-Reitz / GGX. Everything about the highlight's *size* comes out of
// this variance and nothing else, which is the point of the rewrite: the old
// model had a shininess exponent here and a Fresnel exponent there, and the two
// could be set against each other. It takes a2 (the squared lobe variance) rather
// than a roughness because the caller filters that variance first - see the
// screen-space widening in main().
float D_GGX(float NoH, float a2) {
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 1e-7);
}

// Smith height-correlated visibility, in the form that folds the 1/(4 NoV NoL)
// normalisation in - so there is deliberately no separate geometry term below.
float V_SmithGGXCorrelated(float NoV, float NoL, float a2) {
    float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    return 0.5 / max(ggxL + ggxV, 1e-6);
}

// The sun's apparent radius in radians (0.267 deg) and the solid angle of the
// disk it subtends - written as pi*theta^2 rather than as 2*pi*(1-cos theta),
// because in a float shader the latter is a subtraction of two numbers that agree
// to seven digits and would keep two. A point light in an analytic BRDF is the
// other half of the firefly problem fixed below: the disk has a finite size, so a
// lobe broader than the disk cannot legitimately reach the lobe's full peak.
const float kSunAngle = 0.004654;
const float kSunSolidAngle = PI * kSunAngle * kSunAngle;

// Karis' analytic specular antialiasing, in the two pieces a scene like this one
// needs. (1) The wave field's normal is evaluated per *vertex* and interpolated,
// so within a triangle the surface is flat while the real water is sloping every
// which way; a GGX lobe of variance 1.3e-5 against that is a sub-pixel spike, and
// MSAA turns a spike that hops between samples into a white haze over the whole
// basin - which is exactly what "PBR water" looks like when it is wrong. The
// screen-space derivative of the interpolated normal measures that missing slope
// variance, and adding it in quadrature widens the lobe to what the pixel really
// covers. (2) A wider lobe has a lower peak *and* less energy per steradian, so
// the light's finite disk is folded in: once the lobe is broader than the sun, the
// contribution is scaled by the ratio of their solid angles. Without that step the
// highlight would get brighter as the surface got rougher instead of spreading out.
float FilteredLobeVariance(float a2, vec3 n) {
    float sp = dot(fwidth(n), fwidth(n));
    return clamp(a2 + sp, 1e-6, 1.0);
}

float SunClip(float a2) {
    float dMax = 4.0 / a2 - 2.0;
    float lobeSA = 2.0 * PI / (dMax + 2.0 * PI / (140.0 * kSunAngle * kSunAngle));
    return clamp(lobeSA / kSunSolidAngle, 0.0, 1.0);
}

vec3 F_Schlick(float cosT, vec3 f0) {
    float u  = clamp(1.0 - cosT, 0.0, 1.0);
    float u5 = u * u * u * u * u;
    return f0 + (vec3(1.0) - f0) * u5;
}

// The analytic twin of the dome's gradient map: linear in dir.y on either side of
// the horizon, because that is how MakeSkyDesc ramps its three bands. One shape,
// two consumers.
vec3 SkyRadiance(vec3 dir) {
    return dir.y >= 0.0
        ? mix(uSkyHorizon, uSkyZenith, clamp(dir.y, 0.0, 1.0))
        : mix(uSkyHorizon, uSkyNadir,  clamp(-dir.y, 0.0, 1.0));
}

// A rough lobe is a wide cone, so the environment it averages over slides from
// the exact reflection direction toward the hemisphere around the normal. This is
// not a prefiltered cubemap - it is the cheapest stand-in for that angular
// integral, and it is the reason there is no BRDF-integration LUT either: the
// lobe mix carries the energy loss a real split-sum would look up. Both limits
// come out right (blur 0 mirrors the sky, blur 1 gets flat ambient), which is
// all a scene with one sun and a gradient sky can ask for. `blur` is the lobe's
// standard deviation, not its variance, because that is the quantity whose
// endpoints mean "sharp mirror" and "whole hemisphere".
vec3  PrefilteredSky(vec3 R, vec3 N, float blur) {
    return mix(SkyRadiance(R), SkyRadiance(N), blur);
}

void main() {
    // Maps uploaded as sRGB decode to linear radiance inside texture(); alpha is
    // never colour, so it stays linear no matter what the rgb channels are tagged.
    vec4 map = texture(uTex, vUv * uUvScale);

    // Alpha-test geometry: `discard` kills the fragment outright, so it writes
    // neither colour NOR depth. That is why a cutout fence belongs with the
    // opaque layer - the holes are real holes in the depth buffer, and anything
    // behind them is still free to draw, in any order.
    if (map.a < uAlphaCutoff) discard;

    vec3 n = normalize(vNormal);
    // Sheets in this scene are seen from both sides (the water from underneath,
    // a pane from behind). Without the flip the back side is lit by its own
    // inward normal and reads black, which is a shading bug, not a culling one.
    if (!gl_FrontFacing) n = -n;

    // An orthographic camera has no eye to look *from*: every ray in the frustum
    // is parallel, so "vector from the camera to this pixel" is not a coarse
    // approximation, it is undefined. Using the perspective one anyway makes NoV -
    // and therefore the Fresnel, and therefore the conserved alpha - drift across
    // the screen as a pure artefact of the projection, brightest at the edges for
    // a surface that is physically flat. All the parallel rays share one
    // direction, and it is the negated camera forward (the camera looks down -Z).
    // That the grazing mirror band then goes uniform is not a bug to patch: it is
    // what removing perspective actually means.
    vec3 viewDir = uOrthographic > 0.5
                 ? -normalize(uCamForward)
                 : normalize(uEye - vWorldPos);

    vec3  albedo = uAlbedo * map.rgb;
    float NoV = max(dot(n, viewDir), 1e-4);
    float NoL = max(dot(n, uSunDir), 0.0);

    vec3  f0     = vec3(F0FromIor(uIor));
    vec3  F      = F_Schlick(NoV, f0);
    // The single number the whole transparency is built from. A dielectric's f0 is
    // achromatic, so this is not a luminance approximation doing secret work - it
    // is the same value in all three channels: water 2.0 %, glass 4.0 %, soap film
    // 2.9 % - the three the scene actually contains.
    float mirror = clamp(dot(F, THIRD), 0.0, 1.0);

    // One filtered variance feeds the NDF, the visibility term and the environment
    // blur, so a surface cannot have a sharp highlight and a soft reflection at the
    // same time - the kind of inconsistency the four hand-tuned knobs used to allow.
    float r2 = uRoughness * uRoughness;
    float a2 = FilteredLobeVariance(r2 * r2, n);

    vec3  halfVec = normalize(uSunDir + viewDir);
    float NoH     = max(dot(n, halfVec), 0.0);
    vec3  sun     = uSunColor * NoL;
    vec3  spec    = D_GGX(NoH, a2)
                  * V_SmithGGXCorrelated(NoV, NoL, a2) * F * sun * SunClip(a2);
    vec3  kd      = vec3(1.0) - F;
    vec3  diffuse = kd * (albedo / PI) * sun;

    vec3 irr      = SkyRadiance(n);
    vec3 ambDiff  = kd * albedo * irr;
    vec3  ambSpec  = PrefilteredSky(reflect(-viewDir, n), n, sqrt(a2)) * F;

    // Beer-Lambert along the slant path: uPathLen is how much material the ray
    // crosses and 1/NoV is how much longer that crossing is at an angle, clamped
    // so a hair's-width grazing view cannot divide the world into an infinite
    // green. This one line is why the far half of the basin reads as a wall while
    // the water under the camera still shows the checker tiles on the bottom - and
    // there is no texture, no mask and no second shader in that difference.
    float slant   = uPathLen / max(NoV, 0.12);
    vec3  trans   = exp(-uAbsorption * slant);
    float absorb  = 1.0 - dot(trans, THIRD);
    // In-scatter: what the layer hands back rather than mirroring or passing on.
    vec3  body    = kd * trans * albedo * irr;

    vec3 lit   = ambDiff + ambSpec + diffuse + spec + body + albedo * uEmissive;
    vec3 unlit = albedo * (1.0 + uEmissive);
    vec3 c     = mix(unlit, lit, uLightMix);

    if (uIridescence > 0.0) {
        // Thin-film interference, stood up by "hue from the viewing angle, only
        // where the film is edge-on" - which is where a real soap film goes
        // colourful, and now keyed on the derived reflectance rather than on a
        // free exponent. A real one would need the film thickness as a texture.
        vec3 rainbow = 0.5 + 0.5 * cos(vec3(0.0, 2.0943951, 4.1887902) + mirror * 7.0);
        c += uIridescence * rainbow * mirror * 0.55;
    }

    // Coverage, not "an alpha somebody picked". Of the light arriving at this
    // surface, mirror F reflects, (1-F)*absorb is eaten on the way through, and
    // only (1-F)*trans reaches whatever is behind it - so the destination is let
    // through at exactly 1 - alpha and the two halves sum to 1 for every input
    // value. That is what makes five panes, a lake and a bubble stack into a frame
    // that still reads as glass rather than as a grey pile: nothing in the model
    // can invent radiance at composite time. uFresnelToAlpha below 1 is a surface
    // that lies about being a mirror, and the ladder panes use it that way.
    float coverage = mirror * uFresnelToAlpha + (1.0 - mirror) * absorb;
    float alpha = uBaseAlpha + coverage
                + NoV * NoV * NoV * uCoreToAlpha;
    alpha = clamp(alpha, 0.0, 1.0);

    // And the rgb leaves this stage scaled by that coverage, because the two are a
    // matched pair: (ONE, 1-SRC_ALPHA) adds the source without touching it, so a
    // layer that did not pre-multiply would dump its *full* radiance on top of the
    // (1 - a) of background it just let through. Five panes of "10 % glass" then
    // white out the frame - which is not a tuning problem but the arithmetic of
    // adding instead of attenuating. Straight alpha (SRC_ALPHA, 1-SRC_ALPHA) gets
    // uPremultiply off and the fixed-function unit does the same multiply instead:
    // one contract, two places to write it, and with a scalar alpha the two frames
    // are pixel-identical - press `4` and cmp them to confirm. The scaling factor
    // being a single number is what makes them identical; premultiplied is still
    // the one worth running, because it keeps the weighting here, where mirroring
    // a reflection by F and passing a body through by (1 - F) could be two
    // different weights if this scene ever needed them to be.
    //
    // Linear, on purpose: the chain tone-maps and encodes at composite time.
    vec3 rgbOut = (uPremultiply > 0.0) ? c * alpha : c;
    FragColor = vec4(rgbOut, alpha);
}
)GLSL";

} // namespace glass_water

#endif // GLASS_WATER_SHADERS_H
