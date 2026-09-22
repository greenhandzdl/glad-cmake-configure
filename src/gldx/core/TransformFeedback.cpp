module;

#include "gldx/gmf.hpp"

#include <cstdio>
#include <cstdlib>

module gldx;

namespace gldx {

namespace {

// Same posture as RenderContext::AssertRenderThread: these two are programming
// errors that corrupt global GL state rather than recoverable conditions, so they
// say what happened and stop, instead of letting the next unrelated draw inherit it.
[[noreturn]] void Fail(const char* what) {
    std::fprintf(stderr, "[gldx::TransformFeedback] FATAL: %s\n", what);
    std::abort();
}

// True when `tf` is the transform feedback object GL is currently capturing into.
// glBeginTransformFeedback and glBindBufferBase both act on the *current binding*
// rather than on an object passed in, so "did you remember to Bind() this one
// first" is a real question and not a theoretical one.
bool IsBound(GLuint tf) {
    GLint current = 0;
    glGetIntegerv(GL_TRANSFORM_FEEDBACK_BINDING, &current);
    return static_cast<GLuint>(current) == tf;
}

} // namespace

TransformFeedback::~TransformFeedback() {
    if (id_ == 0 && query_ == 0) return;
    RenderContext::AssertRenderThread("~TransformFeedback");

    // Close an unbalanced Begin() rather than assert our way out of it. A transform
    // feedback or query object left open is *global* GL state: the next unrelated
    // draw would capture into the buffers of an object that is about to be deleted,
    // and the query would stay active until someone stumbled into glEndQuery. That
    // is worse than quietly finishing what this object started, and it only happens
    // on a path that already abandoned the frame.
    if (capturing_) {
        if (IsBound(id_)) {
            glEndTransformFeedback();
            glEndQuery(GL_PRIMITIVES_GENERATED);
        }
        capturing_ = false;
    }

    if (query_ != 0) glDeleteQueries(1, &query_);
    if (id_    != 0) glDeleteTransformFeedbacks(1, &id_);
}

TransformFeedback::TransformFeedback(TransformFeedback&& other) noexcept
    : id_(other.id_), query_(other.query_), capturing_(other.capturing_) {
    other.id_        = 0;
    other.query_     = 0;
    other.capturing_ = false;
}

TransformFeedback& TransformFeedback::operator=(TransformFeedback&& other) noexcept {
    if (this != &other) {
        if (id_ != 0 || query_ != 0) {
            RenderContext::AssertRenderThread("TransformFeedback::move=");
            if (capturing_ && IsBound(id_)) {
                glEndTransformFeedback();
                glEndQuery(GL_PRIMITIVES_GENERATED);
            }
            if (query_ != 0) glDeleteQueries(1, &query_);
            if (id_    != 0) glDeleteTransformFeedbacks(1, &id_);
        }
        id_        = other.id_;
        query_     = other.query_;
        capturing_ = other.capturing_;
        other.id_        = 0;
        other.query_     = 0;
        other.capturing_ = false;
    }
    return *this;
}

void TransformFeedback::Create() {
    RenderContext::AssertRenderThread("TransformFeedback::Create");
    if (id_ != 0) {
        glDeleteQueries(1, &query_);
        glDeleteTransformFeedbacks(1, &id_);
    }
    glGenTransformFeedbacks(1, &id_);
    // Created alongside the object it belongs to, so PrimitivesGenerated() works on
    // a freshly created object (reporting "no result yet") instead of needing a
    // second, easy-to-forget initialiser step.
    glGenQueries(1, &query_);
    capturing_ = false;
}

void TransformFeedback::Bind() const {
    RenderContext::AssertRenderThread("TransformFeedback::Bind");
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, id_);
}

void TransformFeedback::Unbind() const {
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0);
}

void TransformFeedback::AttachBuffer(GLuint index, const GLBuffer& buffer) const {
    RenderContext::AssertRenderThread("TransformFeedback::AttachBuffer");
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, index, buffer.id());
}

void TransformFeedback::DetachBuffer(GLuint index) const {
    RenderContext::AssertRenderThread("TransformFeedback::DetachBuffer");
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, index, 0);
}

void TransformFeedback::Begin(GLenum primitiveMode) const {
    RenderContext::AssertRenderThread("TransformFeedback::Begin");
    if (capturing_)                Fail("Begin() while already capturing (End() missing?)");
    if (!IsBound(id_))             Fail("Begin() on an object that is not the bound one");
    glBeginQuery(GL_PRIMITIVES_GENERATED, query_);
    glBeginTransformFeedback(primitiveMode);
    capturing_ = true;
}

void TransformFeedback::End() const {
    RenderContext::AssertRenderThread("TransformFeedback::End");
    if (!capturing_)               Fail("End() without a matching Begin()");
    if (!IsBound(id_))             Fail("End() on an object that is not the bound one");
    glEndTransformFeedback();
    glEndQuery(GL_PRIMITIVES_GENERATED);
    capturing_ = false;
}

bool TransformFeedback::PrimitivesAvailable() const {
    RenderContext::AssertRenderThread("TransformFeedback::PrimitivesAvailable");
    if (query_ == 0 || capturing_) return false;
    GLint available = GL_FALSE;
    glGetQueryObjectiv(query_, GL_QUERY_RESULT_AVAILABLE, &available);
    return available == GL_TRUE;
}

GLuint TransformFeedback::PrimitivesGenerated() const {
    RenderContext::AssertRenderThread("TransformFeedback::PrimitivesGenerated");
    // Reading an active query is an error, and there is no useful answer to give
    // before the first capture session has closed, so both report "nothing".
    if (query_ == 0 || capturing_) return 0;
    GLuint count = 0;
    glGetQueryObjectuiv(query_, GL_QUERY_RESULT, &count);
    return count;
}

} // namespace gldx
