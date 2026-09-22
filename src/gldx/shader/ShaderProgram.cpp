module;

#include "gldx/gmf.hpp"

#include <cstdio>
#include <fstream>

#include <glm/gtc/type_ptr.hpp>

module gldx;

namespace gldx {

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

// Read a whole text file into a string for CreateFromFiles. A crafted or
// oversized file must never drive an unbounded allocation, so bound the size
// first (same posture as Font::LoadFromFile). Only used by the opt-in external
// loader; the engine's own shaders never touch the filesystem.
std::expected<std::string, std::string> ReadSourceFile(std::string_view path) {
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) return std::unexpected("ShaderProgram: cannot open " + std::string(path));
    const std::streamoff size = file.tellg();
    constexpr std::streamoff kMaxShaderBytes = 4LL * 1024 * 1024;   // 4 MB sanity limit
    if (size <= 0 || size > kMaxShaderBytes)
        return std::unexpected("ShaderProgram: invalid or oversized shader file " + std::string(path));
    file.seekg(0, std::ios::beg);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!file.read(text.data(), size))
        return std::unexpected("ShaderProgram: cannot read " + std::string(path));
    return text;
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
ShaderProgram::AssembleFromSources(std::span<const ShaderSource> stages,
                                   const TransformFeedbackDesc* xfb) {
    RenderContext::AssertRenderThread("ShaderProgram::AssembleFromSources");

    // A 4.1 graphics pipeline needs at least a vertex + a fragment stage; the
    // rest are optional intermediates. Guard up front so a malformed assembly
    // gives a clear message instead of an opaque driver link log.
    bool hasVertex = false, hasFragment = false;
    for (const auto& s : stages) {
        hasVertex   = hasVertex   || (s.stage == ShaderStage::Vertex);
        hasFragment = hasFragment || (s.stage == ShaderStage::Fragment);
    }
    if (!hasVertex || !hasFragment)
        return std::unexpected(
            "ShaderProgram::AssembleFromSources: a 4.1 pipeline needs at least a vertex and a fragment stage");

    GLuint prog = glCreateProgram();
    std::vector<GLuint> compiled;
    compiled.reserve(stages.size());
    auto abortWith = [&](std::string msg) {
        for (GLuint sh : compiled) glDeleteShader(sh);
        glDeleteProgram(prog);
        return std::unexpected(std::move(msg));
    };

    for (const auto& s : stages) {
        auto sh = CompileStage(static_cast<GLenum>(s.stage), s.source);
        if (!sh) return abortWith(sh.error());
        compiled.push_back(*sh);
        glAttachShader(prog, *sh);
    }

    // Between the attaches and the link, which is the only window in which GL reads
    // this. A name that is not a captured output of the final stage is a link error
    // on a conformant implementation, so it surfaces through the link log below
    // rather than needing its own check here.
    if (xfb && !xfb->varyings.empty())
        glTransformFeedbackVaryings(prog, static_cast<GLsizei>(xfb->varyings.size()),
                                    xfb->varyings.data(), xfb->bufferMode);

    glLinkProgram(prog);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    // The program has absorbed the shaders; release our references either way.
    for (GLuint sh : compiled) glDeleteShader(sh);
    if (!ok) {
        std::string log = InfoLog(prog, GL_INFO_LOG_LENGTH, "program link error");
        glDeleteProgram(prog);
        return std::unexpected(std::move(log));
    }

    ShaderProgram result;
    result.id_ = prog;
    return result;
}

std::expected<ShaderProgram, std::string>
ShaderProgram::CreateFromSources(std::initializer_list<ShaderSource> stages) {
    return AssembleFromSources(std::span<const ShaderSource>(stages.begin(), stages.size()));
}

std::expected<ShaderProgram, std::string>
ShaderProgram::CreateFromSources(std::initializer_list<ShaderSource> stages,
                                 const TransformFeedbackDesc& xfb) {
    return AssembleFromSources(std::span<const ShaderSource>(stages.begin(), stages.size()),
                               &xfb);
}

std::expected<ShaderProgram, std::string>
ShaderProgram::CreateFromSource(std::string_view vertexSrc, std::string_view fragmentSrc) {
    return CreateFromSources({{ShaderStage::Vertex, vertexSrc},
                              {ShaderStage::Fragment, fragmentSrc}});
}

std::expected<ShaderProgram, std::string>
ShaderProgram::CreateFromFiles(std::initializer_list<ShaderFile> files) {
    // Read every stage first so a missing / oversized file fails cleanly before
    // any GL work. Sources are owned in `texts`; the parallel `stages` point into
    // it and are finalised only once every string is in place, so no reallocation
    // or small-string move can leave a view dangling.
    std::vector<std::string> texts;
    texts.reserve(files.size());
    std::vector<ShaderStage> order;
    order.reserve(files.size());
    for (const auto& f : files) {
        auto src = ReadSourceFile(f.path);
        if (!src) return std::unexpected(src.error());
        texts.push_back(std::move(*src));
        order.push_back(f.stage);
    }
    std::vector<ShaderSource> stages;
    stages.reserve(texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i)
        stages.push_back(ShaderSource{order[i], std::string_view(texts[i])});
    return AssembleFromSources(std::span<const ShaderSource>(stages));
}

std::expected<ShaderProgram, std::string>
ShaderProgram::CreateFromFiles(std::string_view vertexPath, std::string_view fragmentPath) {
    return CreateFromFiles({{ShaderStage::Vertex, vertexPath},
                            {ShaderStage::Fragment, fragmentPath}});
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

void ShaderProgram::SetBlockBinding(const std::string& blockName, GLuint binding) const {
    const GLuint idx = glGetUniformBlockIndex(id_, blockName.c_str());
    if (idx != GL_INVALID_INDEX) {
        glUniformBlockBinding(id_, idx, binding);
    } else {
        RenderContext::AssertRenderThread("ShaderProgram::SetBlockBinding");
        std::fprintf(stderr, "[ShaderProgram] uniform block '%s' not found\n", blockName.c_str());
    }
}

} // namespace gldx
