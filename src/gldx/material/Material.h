#ifndef GLDX_MATERIAL_MATERIAL_H
#define GLDX_MATERIAL_MATERIAL_H

/**
 * @file Material.h
 * @brief Material abstraction for the Phase 2 PBR forward pass.
 *
 * A Material is pure CPU state (parameters + non-owning pointers to already
 * uploaded textures). Apply() pushes it onto a compiled ShaderProgram on the
 * render thread; because it only reads its own members and issues GL calls it
 * is safe to share one Material across many draw calls, but never mutate it
 * off the render thread while a frame is in flight.
 *
 * Texture-unit conventions live in gldx::texunit and mirror the sampler-uniform
 * assignments documented in gldx/shader/ShaderLib.h. A material owns only its
 * four per-object maps; the shadow array and the IBL cubes are bound once per
 * frame by the render pass, not per material.
 */

#include <algorithm>
#include <string>

#include <glm/glm.hpp>

#include "gldx/shader/ShaderProgram.h"
#include "gldx/texture/Texture2D.h"

namespace gldx {

// Fixed texture units shared between materials, the render pass and the PBR
// shader. Keep these in sync with ShaderLib.h's sampler uniforms.
namespace texunit {
inline constexpr unsigned albedo      = 0;
inline constexpr unsigned metalRough  = 1;
inline constexpr unsigned ao          = 2;
inline constexpr unsigned normal      = 3;
inline constexpr unsigned shadowArray = 4;
inline constexpr unsigned irradiance  = 5;
inline constexpr unsigned prefilter   = 6;
inline constexpr unsigned brdfLut     = 7;
inline constexpr unsigned skybox      = 8;   // background cube (drawn separately)
inline constexpr unsigned voxelAtlas  = 9;   // block Texture2DArray (voxel passes)
inline constexpr unsigned particle    = 10;  // point-sprite particle texture
} // namespace texunit

class Material {
public:
    virtual ~Material() = default;

    // Push uniforms + bind owned textures. Render-thread only (touches GL).
    virtual void Apply(const ShaderProgram& program) const = 0;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

protected:
    std::string name_;
};

// Metallic/roughness PBR material matching gldx::shaders::kPbrFragment.
struct PbrMaterial final : Material {
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic   = 0.0f;
    float roughness  = 0.5f;
    float normalScale = 1.0f;
    float alphaCutoff = 0.0f;   // 0 disables alpha-test discard

    // Non-owning: the textures must outlive the material (AssetManager owns them).
    const Texture2D* albedo     = nullptr;
    const Texture2D* metalRough = nullptr;  // G = roughness, B = metallic
    const Texture2D* ao         = nullptr;
    const Texture2D* normal     = nullptr;  // tangent-space

    // A tiny valid RGBA texture (usually 1x1 white) bound to any unit whose map
    // is absent. GLSL validates sampler/texture type matches for *active* units
    // regardless of the uHas* branch, so leaving a wrong-type texture (e.g. a
    // cube from an earlier pass) bound would make the whole unit sample as zero.
    const Texture2D* placeholder = nullptr;

    void Apply(const ShaderProgram& program) const override {
        program.Set("uBaseColor",   baseColor);
        program.Set("uMetallic",    metallic);
        program.Set("uRoughness",   std::clamp(roughness, 0.035f, 1.0f));
        program.Set("uNormalScale", normalScale);
        program.Set("uAlphaCutoff", alphaCutoff);

        program.Set("uAlbedo",     static_cast<int>(texunit::albedo));
        program.Set("uMetalRough", static_cast<int>(texunit::metalRough));
        program.Set("uAo",         static_cast<int>(texunit::ao));
        program.Set("uNormal",     static_cast<int>(texunit::normal));

        BindIf(program, albedo,     texunit::albedo,     "uHasAlbedo");
        BindIf(program, metalRough, texunit::metalRough, "uHasMetalRough");
        BindIf(program, normal,     texunit::normal,     "uHasNormal");
        BindIf(program, ao,         texunit::ao,         "uHasAo");
    }

private:
    void BindIf(const ShaderProgram& program, const Texture2D* tex,
                unsigned unit, const char* flag) const {
        if (tex && tex->valid()) {
            tex->Bind(unit);
            program.Set(flag, 1);
        } else {
            // Keep the unit a real 2D texture so the sampler type always matches.
            if (placeholder && placeholder->valid()) placeholder->Bind(unit);
            program.Set(flag, 0);
        }
    }
};

} // namespace gldx

#endif // GLDX_MATERIAL_MATERIAL_H
