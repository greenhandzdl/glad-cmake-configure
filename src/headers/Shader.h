#ifndef GLFW_TEMPLATE_SHADER_H
#define GLFW_TEMPLATE_SHADER_H

#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <expected>        // C++23；C++20 换 <tl/expected.hpp>
#include <string>
#include <unordered_map>

namespace utils {

    struct ShaderError {
        enum class Kind { FileOpen, Compile, Link };

        Kind        kind;
        std::string file;   // FileOpen/Compile: 出错的文件；Link: "vert, frag"
        std::string log;    // Compile/Link 的日志；FileOpen 时为空

        std::string toString() const;
    };

    class Shader {
    public:
        // 唯一的创建入口：失败通过 expected 返回，不抛异常
        static std::expected<Shader, ShaderError>
        create(const std::string& vertexPath, const std::string& fragmentPath);

        ~Shader();

        // 禁止拷贝；允许移动
        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&& other) noexcept;
        Shader& operator=(Shader&& other) noexcept;

        // 绑定为当前 program（glProgramUniform* 路径下非必需，见下方说明）
        // 若对象已被移动过（id_ == 0）会抛 std::logic_error
        void use() const;

        GLuint id() const noexcept { return id_; }

        // 显式查询：被移动后为 false
        bool valid() const noexcept { return id_ != 0; }

        // ---- uniform setters ----
        // 全部经 loc()，若 id_ == 0 会抛 std::logic_error
        void setBool (const std::string& name, bool value) const;
        void setInt  (const std::string& name, int value) const;
        void setFloat(const std::string& name, float value) const;

        void setVec2(const std::string& name, const glm::vec2& value) const;
        void setVec3(const std::string& name, const glm::vec3& value) const;
        void setVec4(const std::string& name, const glm::vec4& value) const;

        void setMat2(const std::string& name, const glm::mat2& mat) const;
        void setMat3(const std::string& name, const glm::mat3& mat) const;
        void setMat4(const std::string& name, const glm::mat4& mat) const;

    private:
        // 私有构造：只有 create 能调用，且只接受已成功链接的 program
        explicit Shader(GLuint programId) noexcept : id_(programId) {}

        void ensureValid() const;

        // 查找并缓存 uniform location；找不到时打印警告
        GLint loc(const std::string& name) const;
        void  set1i(const std::string& name, int value) const;

        GLuint id_ = 0;
        mutable std::unordered_map<std::string, GLint> uniformCache_;
    };

} // namespace utils

#endif // GLFW_TEMPLATE_SHADER_H