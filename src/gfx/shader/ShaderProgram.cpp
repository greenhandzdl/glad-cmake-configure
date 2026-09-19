#include "gfx/shader/ShaderProgram.h"

#include <array>
#include <vector>
#include <utility>

#include <glm/gtc/type_ptr.hpp>

#include "gfx/core/RenderContext.h"

namespace gfx {

namespace {

std::string InfoLog(GLuint obj, GLenum param, const char* fallback) {
    GLint len = 0;
    // Shaders/programs report log lengths via the *iv queries.
    if (glIsShader(obj)) glGetShaderiv(obj, param, &len);
    else                 glGetProgramiv(obj, param, &len);
    if (len <= 0) return fallback;
    std::vector<char> buf(static_cast<std::size_t>(len) + 1, '\0');
    if (glIsShader(obj)) glGetShaderInfoLog(obj, len, nullptr, buf.data());
    else                 glGetProgramInfoLog(obj, len, nullptr, buf.data());
    return std::string(buf.data());
}

std::expected<GLuint, std::string> CompileStage(GLenum type, std::string_view source) {
    GLuint s = glCreateShader(type);
    const char* src = source.data();
    const GLint  len = static_cast<GLint>(source.size());
    glShaderSource(s, 1, &src, &len);
    glCompileShader(s);
    GLint ok = GL_FALSE;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::string log = InfoLog(s, GL_INFO_LOG_LENGTH, "shader compile error");
        glDeleteShader(s);
        return std::unexpected(std::move(log));
    }
    return s;
}

} // namespace

ShaderProgram::~ShaderProgram() {
    if (id_ != 0) {
        RenderContext::AssertRenderThread("~ShaderProgram");
        glDeleteProgram(id_);
    }
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : id_(other.id_), locCache_(std::move(other.locCache_)) {
    other.id_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (id_ != 0) {
            RenderContext::AssertRenderThread("ShaderProgram::move=");
            glDeleteProgram(id_);
        }
        id_ = other.id_;
        locCache_ = std::move(other.locCache_);
        other.id_ = 0;
    }
    return *this;
}

std::expected<ShaderProgram, std::string>
ShaderProgram::CreateFromSource(std::string_view vertexSrc, std::string_view fragmentSrc) {
    RenderContext::AssertRenderThread("ShaderProgram::CreateFromSource");

    auto vs = CompileStage(GL_VERTEX_SHADER, vertexSrc);
    if (!vs) return std::unexpected(vs.error());
    auto fs = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!fs) { glDeleteShader(*vs); return std::unexpected(fs.error()); }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, *vs);
    glAttachShader(prog, *fs);
    glLinkProgram(prog);

    glDeleteShader(*vs);
    glDeleteShader(*fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::string log = InfoLog(prog, GL_INFO_LOG_LENGTH, "program link error");
        glDeleteProgram(prog);
        return std::unexpected(std::move(log));
    }

    ShaderProgram result;
    result.id_ = prog;
    return result;
}

void ShaderProgram::Use() const {
    RenderContext::AssertRenderThread("ShaderProgram::Use");
    glUseProgram(id_);
}

void ShaderProgram::Unuse() {
    glUseProgram(0);
}

GLint ShaderProgram::Loc(const std::string& name) const {
    auto it = locCache_.find(name);
    if (it != locCache_.end()) return it->second;
    GLint loc = glGetUniformLocation(id_, name.c_str());
    locCache_.emplace(name, loc);
    return loc;
}

void ShaderProgram::Set(const std::string& name, int v) const {
    glProgramUniform1i(id_, Loc(name), v);
}
void ShaderProgram::Set(const std::string& name, float v) const {
    glProgramUniform1f(id_, Loc(name), v);
}
void ShaderProgram::Set(const std::string& name, bool v) const {
    glProgramUniform1i(id_, Loc(name), v ? 1 : 0);
}
void ShaderProgram::Set(const std::string& name, const glm::vec2& v) const {
    glProgramUniform2fv(id_, Loc(name), 1, &v[0]);
}
void ShaderProgram::Set(const std::string& name, const glm::vec3& v) const {
    glProgramUniform3fv(id_, Loc(name), 1, &v[0]);
}
void ShaderProgram::Set(const std::string& name, const glm::vec4& v) const {
    glProgramUniform4fv(id_, Loc(name), 1, &v[0]);
}
void ShaderProgram::Set(const std::string& name, const glm::mat3& v) const {
    glProgramUniformMatrix3fv(id_, Loc(name), 1, GL_FALSE, glm::value_ptr(v));
}
void ShaderProgram::Set(const std::string& name, const glm::mat4& v) const {
    glProgramUniformMatrix4fv(id_, Loc(name), 1, GL_FALSE, glm::value_ptr(v));
}

} // namespace gfx
