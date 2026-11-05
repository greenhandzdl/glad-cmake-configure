#ifndef NIGHT_FOREST_SKY_H
#define NIGHT_FOREST_SKY_H

/**
 * @file Sky.h
 * @brief The night sky as a cube map: a procedurally painted star field, or a
 *        user-supplied star panorama reprojected onto the six faces.
 *
 * The sky is the reason this demo can talk about instancing at night at all -
 * without something bright and structured behind the tree line there is nothing
 * to silhouette against, and the Gooch shadow tone has no context. It is a cube
 * ("box" mapping in the oldest sense of the word): six square faces sampled by
 * direction, drawn by the engine's own SkyboxRenderer and sampled again by the
 * crate when the FBO capture is switched off.
 *
 * Every colour here is a function of the *direction vector alone*, which is what
 * makes the cube seamless for free: a face border pixel and its neighbour on the
 * adjacent face evaluate the same function at the same direction, so there is no
 * edge to blend, no wrapping scheme to keep consistent, and no polar pinch (the
 * direction is never converted to longitude/latitude except in the panorama
 * reprojection, where the conversion is the whole point).
 *
 * The radiance is 16-bit float and deliberately goes above 1.0. The post chain
 * tone-maps with ACES and the bloom stage keys off a threshold, so a star field
 * whose brightest point is 0.8 compresses to a uniform grey sprinkle; the cores
 * need real headroom to read as points of light that bleed.
 *
 * About the panorama path: this project has no network access, so no image file
 * can be fetched here. `--stars PATH` is the seam instead - any 2:1
 * equirectangular night sky (a Milky Way panorama, a rendered HEAVEN box, a
 * photograph) is decoded with the engine's own loader, reprojected, linearised
 * and scaled into the same HDR cube. The procedural field is what the demo ships
 * with, and it is the frame the documentation's screenshots come from.
 *
 * NOTE: names gldx engine types (TextureCubeDesc), so include AFTER
 * `import gldx;`.
 */

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace night_forest {

struct StarField {
    // Square face=s; 6 * size^2 texels, painted on the CPU. 512 is what the window
    // resolution asks for: an 800x600 view on a retina display magnifies the cube
    // by three, and bilinear filtering a 256 face that hard shows the texel grid
    // as a diamond pattern in the sky. Measured, unoptimised build, this machine:
    // the paint costs 1.7 s at 512 and 0.3 s at 128, and it is almost all the
    // per-texel half of the job - one direction and one moon tap per texel - which
    // is why the star count and the galactic noise are not what a bigger cube pays
    // for. 1.7 s is the price of the frame the documentation shows; --star-size is
    // the knob for anyone who would rather start faster.
    int  size = 512;
    std::uint32_t seed = 7719u;         // the field is reproducible by seed alone

    // Fraction of the cube's texels that holds a star. The count therefore scales
    // with size^2, and that is the invariant a backdrop wants: what stays constant
    // is stars per square degree of sky, so a bigger cube is the same night
    // sharper, not the night busier. Fixing the count instead would make every star
    // sub-texel as the cube grew, and a sub-texel star sampled by direction is a
    // star that may or may not land on a texel centre at all.
    //
    // The value itself is set by eye against the frame, and what it is set against
    // is the empty sky between the stars. At 0.0035 the field is bright enough to
    // be a sky but every patch of it is inside a star's glow, the tone mapper lifts
    // the whole hemisphere, and the night fogs over; 0.002 at 512 is about fifteen
    // hundred points, which is roughly what a dark site actually shows and leaves
    // stretches of nothing between them. Nothing is what makes a star field read as
    // one.
    float density = 0.002f;
    // HDR peak of the very brightest cores. Most stars land a fraction of this:
    // the splat weights each one by a power law, so this number is what the top
    // few percent reach, and it is set against the bloom threshold rather than in
    // search of a nice round three - a peak that clears the threshold by a third
    // glows, one that clears it by four does not.
    float brightness = 3.0f;
    float galaxy = 0.6f;                // milky-way band strength
    float gain = 1.0f;                  // multiplies whatever the image provides

    // The moon. Its direction is also the scene's directional light (main.cpp
    // negates it for LightSetup::sun.direction), so the disc in the sky and the
    // shading on the trees cannot disagree - the single most common way a lit
    // night scene reads as a matte-painted box.
    glm::vec3 moonDir{0.42f, 0.46f, -0.78f};
    float moonRadius = 0.05f;           // radians; generously larger than reality
    float moonIntensity = 14.0f;        // HDR radiance of the disc
};

// Paint the procedural field. Stage A - no GL, so a worker thread can run it
// while the rest of the scene is being built.
gldx::TextureCubeDesc MakeStarCubeDesc(const StarField& field);

// Reproject a 2:1 equirectangular panorama onto the six faces and composite the
// moon disc on top (an image without a light source leaves the directional
// shading with nothing to point at). `field.gain` scales the image into the
// scene's HDR range; `field.moonIntensity <= 0` skips the disc. On failure
// `err` carries the reason and the return is false. Stage A, no GL.
bool LoadEquirectCubeDesc(const std::string& path, const StarField& field,
                          gldx::TextureCubeDesc& out, std::string& err);

// The moon's direction, normalised - the one place that decides both the painted
// disc and the sun vector, so the two cannot drift.
glm::vec3 MoonDirection(const StarField& field);

// Which texel of which face a direction lands in is not this demo's decision to
// make, so it does not carry a table of its own any more: the six face frames
// {right, up, facing} live in the engine as gldx::kCubeFaceAxis /
// gldx::CubeFaceAxis(face, which) (TextureCubeMap.h, whose comment records that
// the table was measured on this driver and what goes wrong quietly if a second
// copy disagrees with it). The CPU star field, the panorama reprojection and the
// crate's own six-face capture all read that one table, which is what keeps the
// sky this demo paints and the sky its shaders sample in the same place.
using gldx::CubeFaceAxis;
using gldx::CubeFaceTexelDirection;

// The direction at the centre of one texel: column i and row j of an N x N face,
// with row 0 the first row handed to glTexImage2D (which is t = 0).
inline glm::vec3 FaceTexelCenter(int face, int i, int j, int n) {
    return CubeFaceTexelDirection(face, static_cast<float>(i) + 0.5f,
                                  static_cast<float>(j) + 0.5f, n);
}

} // namespace night_forest

#endif // NIGHT_FOREST_SKY_H
