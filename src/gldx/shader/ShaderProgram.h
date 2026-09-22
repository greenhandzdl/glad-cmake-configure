#ifndef GLDX_SHADER_SHADERPROGRAM_H
#define GLDX_SHADER_SHADERPROGRAM_H

/**
 * @file ShaderProgram.h
 * @brief RAII, move-only GLSL program with uniform setters and multi-stage
 *        assembly (vertex + fragment, optionally geometry / tessellation).
 *
 * Created on the render thread (compilation/linking are GL calls). Uniform
 * setters use the OpenGL 4.1 glProgramUniform* family, so they do not require
 * the program to be bound. Failures are reported via std::expected (project
 * convention), never exceptions.
 */

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glad/gl.h>
#include <glm/glm.hpp>

namespace gldx {

// A GLSL pipeline stage offered by the 4.1 core profile. The underlying value
// IS the GL enum, so a ShaderStage casts straight into glCreateShader.
// NOTE: no Compute member — GL_COMPUTE_SHADER is OpenGL 4.3+, above this
// project's 4.1 core baseline (macOS' ceiling), and the GLAD 4.1 loader does
// not even define the constant. Add it only if the baseline is ever raised.
enum class ShaderStage : GLenum {
    Vertex         = GL_VERTEX_SHADER,
    Fragment       = GL_FRAGMENT_SHADER,
    Geometry       = GL_GEOMETRY_SHADER,
    TessControl    = GL_TESS_CONTROL_SHADER,
    TessEvaluation = GL_TESS_EVALUATION_SHADER,
};

// One source / one file paired with its stage, for the generalised assembly
// entry points below. Aggregate types on purpose so you can write brace lists.
struct ShaderSource { ShaderStage stage; std::string_view source; };
struct ShaderFile   { ShaderStage stage; std::string_view path;   };

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&)            = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    // ---- generalised, selective multi-stage assembly ----
    // A 4.1 graphics program needs at least a vertex and a fragment shader;
    // geometry / tessellation control / tessellation evaluation are optional
    // intermediates you slot in by adding a ShaderSource for each. List order is
    // irrelevant and each stage may appear at most once; a compile or link
    // failure yields std::unexpected (the info log), never an exception, and every
    // partially-created shader is released. Render-thread only (it links).
    static std::expected<ShaderProgram, std::string>
    CreateFromSources(std::initializer_list<ShaderSource> stages);

    // File-backed twin: reads every listed file first (so a missing / unreadable
    // / oversized path fails before any GL work), then assembles via
    // CreateFromSources. Same opt-in posture as the two-path overload below.
    static std::expected<ShaderProgram, std::string>
    CreateFromFiles(std::initializer_list<ShaderFile> files);

    // ---- two-stage conveniences (forward to the generalised paths) ----
    static std::expected<ShaderProgram, std::string>
    CreateFromSource(std::string_view vertexSrc, std::string_view fragmentSrc);

    // Opt-in loader for *external* GLSL (e.g. a demo or a user-authored shader
    // hot-loaded from disk): reads the two files (UTF-8/raw bytes) and forwards
    // to CreateFromSource. This is deliberately additive — the engine's own
    // shaders stay embedded as the single source of truth in
    // src/gldx/shader/*Shaders.h (see src/assets/shaders/README.md), so normal
    // builds keep their "zero runtime path / working-directory dependency"
    // guarantee and CI stays safe. Runs on the render thread (it links a
    // program); a missing / unreadable / oversized file yields std::unexpected,
    // never an exception.
    static std::expected<ShaderProgram, std::string>
    CreateFromFiles(std::string_view vertexPath, std::string_view fragmentPath);

    void Use() const;
    static void Unuse();

    [[nodiscard]] GLuint id()    const noexcept { return id_; }
    [[nodiscard]] bool   valid() const noexcept { return id_ != 0; }

    // ---- uniform setters (no bind required) ----
    void Set(const std::string& name, int v) const;
    void Set(const std::string& name, float v) const;
    void Set(const std::string& name, bool v) const;
    void Set(const std::string& name, const glm::vec2& v) const;
    void Set(const std::string& name, const glm::vec3& v) const;
    void Set(const std::string& name, const glm::vec4& v) const;
    void Set(const std::string& name, const glm::mat3& v) const;
    void Set(const std::string& name, const glm::mat4& v) const;

    // Associate a named uniform block with a UBO binding point. GLSL 4.10 has
    // no layout(binding=...) on blocks, so the mapping is done explicitly from
    // C++ (mirrors what UniformBuffer::BindBase(index) later binds against).
    void SetBlockBinding(const std::string& blockName, GLuint binding) const;

private:
    GLint Loc(const std::string& name) const;

    // Core the public entry points forward to. Takes stage/source pairs whose
    // string_views stay valid for the duration of the call; the initializer_list
    // overloads wrap theirs in a span, the file overload owns them in a vector.
    static std::expected<ShaderProgram, std::string>
    AssembleFromSources(std::span<const ShaderSource> stages);

    GLuint id_ = 0;
    mutable std::unordered_map<std::string, GLint> locCache_;
};

} // namespace gldx

#endif // GLDX_SHADER_SHADERPROGRAM_H
