#ifndef GLDX_SHADER_SHADERPROGRAM_H
#define GLDX_SHADER_SHADERPROGRAM_H

/**
 * @file ShaderProgram.h
 * @brief RAII, move-only GLSL program (vertex + fragment) with uniform setters.
 *
 * Created on the render thread (compilation/linking are GL calls). Uniform
 * setters use the OpenGL 4.1 glProgramUniform* family, so they do not require
 * the program to be bound. Failures are reported via std::expected (project
 * convention), never exceptions.
 */

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>

#include <glad/gl.h>
#include <glm/glm.hpp>

namespace gldx {

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&)            = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    static std::expected<ShaderProgram, std::string>
    CreateFromSource(std::string_view vertexSrc, std::string_view fragmentSrc);

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

    GLuint id_ = 0;
    mutable std::unordered_map<std::string, GLint> locCache_;
};

} // namespace gldx

#endif // GLDX_SHADER_SHADERPROGRAM_H
