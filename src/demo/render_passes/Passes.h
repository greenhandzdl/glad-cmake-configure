#ifndef RENDER_PASSES_PASSES_H
#define RENDER_PASSES_PASSES_H

/**
 * @file Passes.h
 * @brief A family of gldx::RenderPass subclasses, one per primitive, assembled
 *        into a window's renderer by a for-loop over a factory table.
 *
 * The demo exists to exercise two things at once: (1) deriving several distinct
 * RenderPass types rather than one parameterised class, and (2) wiring them into
 * a Renderer by iterating a table. A ClearPass runs first (so pass ORDER is
 * visible), then one ShapePass subclass per shape (triangle / quad / lines /
 * points) draws its primitive, animated by the shared clock. Each window builds
 * its own chain on its own context, so the same table yields N independent pass
 * object sets living in N GL contexts.
 */

#include <glad/gl.h>   // GLenum / GLsizei / GLfloat in the ctor + member types
#include <memory>
#include <span>
#include <vector>

import gldx;

#include "SharedState.h"

namespace render_passes {

// Base for one shape drawn straight at the default framebuffer. Each subclass
// only decides its vertex table and GL primitive; the shared work - compiling a
// tiny 2D program, uploading a VBO/VAO, sampling the shared clock, and (for
// points) toggling GL_PROGRAM_POINT_SIZE - lives here. All GL happens in the
// ctor, which the View runs inside its window's OnCreate, so every handle names
// an object on that window's own context and nothing is ever shared cross-context.
class ShapePass : public gldx::RenderPass {
public:
    ShapePass(const char* name, SharedState* state,
              GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b,
              GLenum primitive, std::span<const GLfloat> verts);

    void Execute(gldx::RenderFrame& frame) override;

protected:
    SharedState* state_;
    GLfloat      cx_, cy_, cr_, cg_, cb_;
    GLenum       primitive_;
    GLsizei      vertexCount_ = 0;
    std::unique_ptr<gldx::ShaderProgram> program_;   // null => compile failed, Execute no-ops
    gldx::VertexArray vao_;
    gldx::GLBuffer    vbo_;
};

// One subclass per primitive. The whole point: the Renderer is fed *different*
// RenderPass types in a loop, not one class told what to draw.
class TrianglePass : public ShapePass {
public:
    TrianglePass(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b);
};
class QuadPass : public ShapePass {
public:
    QuadPass(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b);
};
class LinePass : public ShapePass {
public:
    LinePass(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b);
};
class PointPass : public ShapePass {
public:
    PointPass(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b);
};

// First pass of every chain: wipes the framebuffer to a dark base so the shapes
// read against something. A shape pass never clears, so ordering is observable.
class ClearPass : public gldx::RenderPass {
public:
    ClearPass() : gldx::RenderPass("Clear") {}
    void Execute(gldx::RenderFrame& frame) override;
};

// A table entry: an NDC slot + colour + a factory that constructs a *specific*
// ShapePass subclass. The View loops over ShapeTable() and AddPass()es the
// result - this is the "one for loop, many render-pass subclasses" the demo
// exists to prove out. Array order is the pass execution order.
struct ShapeSpec {
    const char* label;
    GLfloat cx, cy, r, g, b;
    std::unique_ptr<ShapePass> (*make)(SharedState*, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat);
};

const std::vector<ShapeSpec>& ShapeTable();

} // namespace render_passes

#endif // RENDER_PASSES_PASSES_H
