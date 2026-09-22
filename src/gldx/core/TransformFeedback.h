#ifndef GLDX_CORE_TRANSFORMFEEDBACK_H
#define GLDX_CORE_TRANSFORMFEEDBACK_H

/**
 * @file TransformFeedback.h
 * @brief RAII, move-only Transform Feedback object wrapper.
 *
 * The capture-side twin of VertexArray: one GL id, one owner, render-thread only.
 * A transform feedback object records which buffers the captured varyings land in
 * and, separately, how many primitives the draws routed through it generated. The
 * second half is the reason this is a class rather than a couple of raw calls: the
 * count lives on the object, so a later VertexArray::DrawTransformFeedback can draw
 * a number that no CPU-side code ever computed.
 *
 * The object also owns a private GL_PRIMITIVES_GENERATED query, opened by Begin()
 * and closed by End(). That is the only way to read the count back, and folding it
 * in here means a caller cannot forget the query the way it could forget a separate
 * one - at the price of one rule: Begin() and End() must pair, because a query
 * cannot be left open across another object's Begin().
 *
 * Under OpenGL 4.1 the capture target itself has to be declared from C++ before the
 * program links (see ShaderProgram::TransformFeedbackDesc): the per-attribute
 * layout(xfb_buffer / xfb_stride) qualifiers are GLSL 4.30 and a 4.10 core shader
 * rejects them, verified on the Apple 4.1 Metal driver this project targets.
 */

#include <glad/gl.h>

namespace gldx {

class GLBuffer;

class TransformFeedback {
public:
    TransformFeedback() = default;
    ~TransformFeedback();

    TransformFeedback(const TransformFeedback&)            = delete;
    TransformFeedback& operator=(const TransformFeedback&) = delete;
    TransformFeedback(TransformFeedback&& other) noexcept;
    TransformFeedback& operator=(TransformFeedback&& other) noexcept;

    void Create();                 // glGenTransformFeedbacks (render thread)
    void Bind() const;             // glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, id_)
    void Unbind() const;

    // Route captured varying binding point `index` into `buffer`. Bind() first:
    // glBindBufferBase records against whichever transform feedback object is
    // current, not against a passed-in id.
    void AttachBuffer(GLuint index, const GLBuffer& buffer) const;
    void DetachBuffer(GLuint index) const;

    // Wrap the draws whose output should be captured. Both assert that *this* is the
    // bound object, because glBeginTransformFeedback acts on the current binding and
    // would otherwise silently capture into a different object's buffers.
    void Begin(GLenum primitiveMode = GL_POINTS) const;
    void End() const;

    // Primitives captured by the most recent Begin()/End() pair. Reading the result
    // waits for the GPU, so only call it once the value is needed rather than on the
    // hot path; PrimitivesAvailable() answers "is it there yet" without waiting, for
    // callers happy to display last frame's number meanwhile. Both are render-thread
    // only, and both report 0 before the first Begin()/End().
    [[nodiscard]] bool PrimitivesAvailable() const;
    [[nodiscard]] GLuint PrimitivesGenerated() const;

    [[nodiscard]] GLuint id()    const noexcept { return id_; }
    [[nodiscard]] bool   valid() const noexcept { return id_ != 0; }

private:
    GLuint id_    = 0;
    GLuint query_ = 0;                 // GL_PRIMITIVES_GENERATED, owned alongside id_
    mutable bool capturing_ = false;   // guards the Begin/End and query pairing
};

} // namespace gldx

#endif // GLDX_CORE_TRANSFORMFEEDBACK_H
