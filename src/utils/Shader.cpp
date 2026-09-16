#include "Shader.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace utils {

    // ============================================================
    //  内部工具：shader stage 的 RAII 包装
    //  - 编译失败：内部先 glDeleteShader，再返回 unexpected
    //  - 编译成功：持有 GLuint，析构时 glDeleteShader
    //  - 不可拷贝，可移动（移动后源对象 id_ = 0）
    // ============================================================
    namespace detail {

        class ShaderStage {
        public:
            ShaderStage() = delete;
            ShaderStage(const ShaderStage&) = delete;
            ShaderStage& operator=(const ShaderStage&) = delete;

            ShaderStage(ShaderStage&& o) noexcept : id_(o.id_) { o.id_ = 0; }

            ShaderStage& operator=(ShaderStage&& o) noexcept {
                if (this != &o) {
                    if (id_) glDeleteShader(id_);
                    id_ = o.id_;
                    o.id_ = 0;
                }
                return *this;
            }

            ~ShaderStage() {
                if (id_) glDeleteShader(id_);
            }

            GLuint id() const noexcept { return id_; }

            static std::expected<ShaderStage, ShaderError>
            compile(GLenum type, const std::string& src, const std::string& path) {
                GLuint s = glCreateShader(type);
                if (s == 0) {
                    return std::unexpected(ShaderError{
                        ShaderError::Kind::Compile, path,
                        "glCreateShader returned 0"});
                }

                const char* p = src.c_str();
                glShaderSource(s, 1, &p, nullptr);
                glCompileShader(s);

                GLint ok = 0;
                glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
                if (!ok) {
                    char log[1024];
                    glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                    glDeleteShader(s);   // 失败立即清理，不依赖 RAII（此时还没构造成功）
                    return std::unexpected(ShaderError{
                        ShaderError::Kind::Compile, path, std::string(log)});
                }

                return ShaderStage(s);
            }

        private:
            explicit ShaderStage(GLuint id) noexcept : id_(id) {}
            GLuint id_ = 0;
        };

    } // namespace detail

    // ============================================================
    //  ShaderError
    // ============================================================

    std::string ShaderError::toString() const {
        const char* k = "Unknown";
        switch (kind) {
            case Kind::FileOpen: k = "FileOpen"; break;
            case Kind::Compile:  k = "Compile";  break;
            case Kind::Link:     k = "Link";     break;
        }
        std::string s = std::string("[") + k + "] " + file;
        if (!log.empty()) {
            s += "\n";
            s += log;
        }
        return s;
    }

    // ============================================================
    //  Shader::create —— 唯一的构造路径
    //
    //  生命周期检查（每一条失败路径）：
    //  ┌─ readFile(vert) 失败 → 无任何 GL 资源创建
    //  ├─ readFile(frag) 失败 → 无任何 GL 资源创建
    //  ├─ compile vert 失败   → ShaderStage::compile 内部已清理
    //  ├─ compile frag 失败   → vs 由 RAII 自动 glDeleteShader
    //  ├─ glCreateProgram=0   → vs/fs 由 RAII 自动清理
    //  ├─ link 失败           → 手动 glDeleteProgram(prog)；
    //  │                        vs/fs 由 RAII 自动清理
    //  └─ link 成功           → Shader(prog) 接管所有权；
    //                           vs/fs 由 RAII 自动清理
    // ============================================================
    std::expected<Shader, ShaderError>
    Shader::create(const std::string& vertexPath, const std::string& fragmentPath) {

        // ---- 0. 读取文件（无 GL 资源，安全） ----
        auto readFile = [](const std::string& path)
                -> std::expected<std::string, ShaderError> {
            std::ifstream f(path, std::ios::binary);
            if (!f) {
                return std::unexpected(ShaderError{
                    ShaderError::Kind::FileOpen, path, {}});
            }
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        };

        auto vsrc = readFile(vertexPath);
        if (!vsrc) return std::unexpected(vsrc.error());

        auto fsrc = readFile(fragmentPath);
        if (!fsrc) return std::unexpected(fsrc.error());

        // ---- 1. 编译两个 stage ----
        auto vs = detail::ShaderStage::compile(GL_VERTEX_SHADER, *vsrc, vertexPath);
        if (!vs) return std::unexpected(vs.error());

        auto fs = detail::ShaderStage::compile(GL_FRAGMENT_SHADER, *fsrc, fragmentPath);
        if (!fs) return std::unexpected(fs.error());
        // 注意：此刻若 fs 失败返回，vs 已在栈上，会被 ShaderStage 析构 -> glDeleteShader

        // ---- 2. 创建并链接 program ----
        GLuint prog = glCreateProgram();
        if (prog == 0) {
            return std::unexpected(ShaderError{
                ShaderError::Kind::Link,
                vertexPath + ", " + fragmentPath,
                "glCreateProgram returned 0"});
        }

        glAttachShader(prog, vs->id());
        glAttachShader(prog, fs->id());
        glLinkProgram(prog);

        GLint success = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &success);
        if (!success) {
            char log[1024];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            glDeleteProgram(prog);   // 立即释放 program
            // vs / fs 由 RAII 自动释放
            return std::unexpected(ShaderError{
                ShaderError::Kind::Link,
                vertexPath + ", " + fragmentPath,
                std::string(log)});
        }

        // ---- 3. link 成功，detach（可选），把所有权交给 Shader ----
        glDetachShader(prog, vs->id());
        glDetachShader(prog, fs->id());
        // vs / fs 出作用域 -> 自动 glDeleteShader

        // 这里到函数返回之间不会再有任何失败路径：
        //  Shader 构造是 noexcept，移动构造是 noexcept
        return Shader(prog);
    }

    // ============================================================
    //  生命周期：析构 / 移动
    // ============================================================

    Shader::~Shader() {
        if (id_) glDeleteProgram(id_);
    }

    Shader::Shader(Shader&& other) noexcept
        : id_(other.id_), uniformCache_(std::move(other.uniformCache_)) {
        other.id_ = 0;   // 被移动对象成为 valid()==false 的空壳
    }

    Shader& Shader::operator=(Shader&& other) noexcept {
        if (this != &other) {
            if (id_) glDeleteProgram(id_);   // 释放自己的旧 program
            id_ = other.id_;
            uniformCache_ = std::move(other.uniformCache_);
            other.id_ = 0;
        }
        return *this;
    }

    // ============================================================
    //  运行时校验
    // ============================================================

    void Shader::ensureValid() const {
        if (id_ == 0) {
            throw std::logic_error(
                "Shader: operation on a moved-from or invalid Shader");
        }
    }

    void Shader::use() const {
        ensureValid();
        glUseProgram(id_);
    }

    // ============================================================
    //  uniform setters
    //
    //  使用 glProgramUniform*：需要 OpenGL 4.5 或 ARB_separate_shader_objects。
    //  macOS 最高 OpenGL 4.1，不支持。见下方"macOS 兼容"。
    // ============================================================

    void Shader::setBool(const std::string& name, bool value) const {
        set1i(name, static_cast<int>(value));
    }

    void Shader::setInt(const std::string& name, int value) const {
        set1i(name, value);
    }

    void Shader::setFloat(const std::string& name, float value) const {
        glProgramUniform1f(id_, loc(name), value);
    }

    void Shader::setVec2(const std::string& name, const glm::vec2& value) const {
        glProgramUniform2fv(id_, loc(name), 1, &value[0]);
    }

    void Shader::setVec3(const std::string& name, const glm::vec3& value) const {
        glProgramUniform3fv(id_, loc(name), 1, &value[0]);
    }

    void Shader::setVec4(const std::string& name, const glm::vec4& value) const {
        glProgramUniform4fv(id_, loc(name), 1, &value[0]);
    }

    void Shader::setMat2(const std::string& name, const glm::mat2& mat) const {
        glProgramUniformMatrix2fv(id_, loc(name), 1, GL_FALSE, &mat[0][0]);
    }

    void Shader::setMat3(const std::string& name, const glm::mat3& mat) const {
        glProgramUniformMatrix3fv(id_, loc(name), 1, GL_FALSE, &mat[0][0]);
    }

    void Shader::setMat4(const std::string& name, const glm::mat4& mat) const {
        glProgramUniformMatrix4fv(id_, loc(name), 1, GL_FALSE, &mat[0][0]);
    }

    // ============================================================
    //  内部
    // ============================================================

    GLint Shader::loc(const std::string& name) const {
        ensureValid();

        auto it = uniformCache_.find(name);
        if (it != uniformCache_.end()) return it->second;

        GLint l = glGetUniformLocation(id_, name.c_str());
        if (l < 0) {
            std::cerr << "Warning: uniform '" << name
                      << "' not found in program " << id_ << "\n";
        }
        uniformCache_.emplace(name, l);
        return l;
    }

    void Shader::set1i(const std::string& name, int value) const {
        glProgramUniform1i(id_, loc(name), value);
    }

} // namespace utils