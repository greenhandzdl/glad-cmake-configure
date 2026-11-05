#ifndef GLDX_TEXTURE_TEXTURECUBEMAP_H
#define GLDX_TEXTURE_TEXTURECUBEMAP_H

/**
 * @file TextureCubeMap.h
 * @brief RAII, move-only cube-map texture (skybox + IBL environment).
 *
 * Supports both 8-bit sRGB/RGBE-free colour faces and 16-bit float HDR faces
 * (needed as the IBL source). Faces are staged CPU-side as a CubeDesc (Stage A,
 * 6 x RGBA/Half rows) and uploaded on the render thread (Stage B).
 */

#include <cstdint>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

namespace gldx {

// The cube-map face convention, kept in this one place for the whole engine.
//
// Which direction a cube texel stands for is fixed by OpenGL, not by taste: section
// 8.13 picks the face from the major axis and takes (sc, tc) as the two remaining
// signed components, giving s = (1 + sc/|ma|)/2 and t = (1 + tc/|ma|)/2. Written
// as the per-face frame {right, up, facing} that produces the same mapping, the
// direction of the texel at face image coordinates (s,t) in [0,1] is
//     normalize(facing + (2s-1)*right + (2t-1)*up)
// and a camera rendering into that face is lookAt(eye, eye + facing, up) under a
// 90-degree frustum - a right-handed camera's screen-right is cross(facing, up),
// which is the `right` listed here.
//
// The table was measured as well as quoted: candidate conventions were written into
// a cube as colour gradients and read back through texture(samplerCube, direction)
// on this driver (GL 4.1 Metal), and only this one returned the face, the s-axis
// and the t-axis it was asked for on all six faces.
//
// Getting it wrong is quiet, which is why it stayed wrong for so long. A generator
// that writes every face with one basis and a shader that reads with the same basis
// never sees a torn seam when the content is smooth, so nothing local looks broken;
// what is broken is the *placement* - light painted into the sky and the light the
// shading points at end up in different places. Measured straight off the GPU with
// EnvironmentMap's own sun disc before this table became the engine's only one: the
// requested direction and the direction a sampler actually returned were 8 degrees
// apart for a sun dead ahead of a face centre and 172 degrees apart for the two
// faces whose `facing` was written with the wrong sign.
//
// Face order is the GL enum order, so `face` indexes kCubeFaceTargets too.
inline constexpr float kCubeFaceAxis[6][3][3] = {
    /* +X */ {{0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
    /* -X */ {{0.0f, 0.0f,  1.0f}, {0.0f, -1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}},
    /* +Y */ {{1.0f, 0.0f,  0.0f}, {0.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}},
    /* -Y */ {{1.0f, 0.0f,  0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
    /* +Z */ {{1.0f, 0.0f,  0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    /* -Z */ {{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
};

// The same table as a frame of vec3s. Stored as floats so the constant needs no
// constexpr constructor, and spelled out here rather than through glm's type_ptr
// so the module interface does not have to pull that header in.
inline glm::vec3 CubeFaceAxis(int face, int which) {
    return glm::vec3(kCubeFaceAxis[face][which][0], kCubeFaceAxis[face][which][1],
                     kCubeFaceAxis[face][which][2]);
}

// The direction handed back to a shader that samples texel (s, t) of `face`, both
// in [0,1]. Row 0 of the image data a face is uploaded with is t = 0.
inline glm::vec3 CubeFaceDirection(int face, float s, float t) {
    const glm::vec3 r = CubeFaceAxis(face, 0);
    const glm::vec3 u = CubeFaceAxis(face, 1);
    const glm::vec3 f = CubeFaceAxis(face, 2);
    return glm::normalize(f + (2.0f * s - 1.0f) * r + (2.0f * t - 1.0f) * u);
}

// The same mapping for a fractional texel position: `i` and `j` counted in texels
// from a face's own origin, over an N x N face. Fractional on purpose - a splat
// whose centre is rounded onto the grid it is stored in shows that grid the moment
// it is wider than one texel.
inline glm::vec3 CubeFaceTexelDirection(int face, float i, float j, int n) {
    return CubeFaceDirection(face, i / static_cast<float>(n), j / static_cast<float>(n));
}

// Cube face targets in the canonical +X -X +Y -Y +Z -Z order, i.e. the order
// kCubeFaceAxis is written in. Exported because everything that attaches or uploads
// faces needs it, and three private copies of "the GL enum order" agreeing only by
// luck is part of how a wrong basis went unnoticed.
inline constexpr GLenum kCubeFaceTargets[6] = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};

struct TextureCubeDesc {
    int  size = 0;              // square faces
    bool hdr  = false;         // true => GL_RGB16F faces from halfData
    bool srgb = false;         // colour LDR faces use sRGB internal format
    std::vector<std::uint8_t> rgba;  // 6 * size*size * 4 bytes (LDR path)
    std::vector<std::uint16_t> half; // 6 * size*size * 4 halfs (HDR path)
};

class TextureCubeMap {
public:
    TextureCubeMap() = default;
    ~TextureCubeMap();

    TextureCubeMap(const TextureCubeMap&)            = delete;
    TextureCubeMap& operator=(const TextureCubeMap&) = delete;
    TextureCubeMap(TextureCubeMap&& other) noexcept;
    TextureCubeMap& operator=(TextureCubeMap&& other) noexcept;

    void Upload(const TextureCubeDesc& desc);   // render-thread only

    // Allocate an empty cube (render target for IBL generation). Render-thread.
    void AllocateCube(GLenum internalFormat, int size, int levels, GLenum format, GLenum type);

    void GenerateMipmaps();                      // render-thread only
    void Bind(unsigned slot = 0) const;
    static void Unbind(unsigned slot);

    [[nodiscard]] GLuint id()    const noexcept { return id_; }
    [[nodiscard]] int    size()  const noexcept { return size_; }
    [[nodiscard]] int    levels()const noexcept { return levels_; }
    [[nodiscard]] bool   valid() const noexcept { return id_ != 0; }

private:
    GLuint id_ = 0;
    int size_ = 0;
    int levels_ = 1;
};

} // namespace gldx

#endif // GLDX_TEXTURE_TEXTURECUBEMAP_H
