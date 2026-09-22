// render_passes demo — the RenderPass family + factory table (see Passes.h).
#include "gldx/core/Platform.h"

#include "Passes.h"

#include <cstdio>

namespace render_passes {

namespace {

// A tiny 2D program shared by every shape pass (each pass compiles its own
// instance, since programs are per-context). It rotates a local-space shape by
// the shared phase, divides x by the aspect so shapes stay square on any window,
// translates it to uCx/uCy in NDC, and shades a flat colour with a soft centre.
// gl_PointSize is set unconditionally so the point pass lights up once the
// GL_PROGRAM_POINT_SIZE cap is enabled (macOS ignores it otherwise).
constexpr const char* kShapeVert = R"GLSL(#version 410 core
layout(location = 0) in vec2 aPos;
uniform float uPhase, uAspect, uCx, uCy;
out vec2 vLocal;
void main() {
    float a = uPhase;
    mat2 rot = mat2(cos(a), -sin(a), sin(a), cos(a));
    vec2 p = rot * aPos;
    p.x /= uAspect;
    vLocal = aPos;
    gl_Position = vec4(p + vec2(uCx, uCy), 0.0, 1.0);
    gl_PointSize = 14.0;
}
)GLSL";

constexpr const char* kShapeFrag = R"GLSL(#version 410 core
in vec2 vLocal;
uniform float uR, uG, uB;
out vec4 FragColor;
void main() {
    float d = clamp(1.0 - length(vLocal) * 1.5, 0.0, 1.0);
    FragColor = vec4(vec3(uR, uG, uB) * (0.55 + 0.45 * d), 1.0);
}
)GLSL";

// Local-space vertex tables (interleaved x,y), each within roughly [-0.26,0.26]
// so the shapes sit side by side once placed at their NDC centres.
constexpr GLfloat kTriangle[] = { 0.00f, 0.26f,  -0.24f, -0.18f,  0.24f, -0.18f };
constexpr GLfloat kQuad[]     = { -0.22f, -0.22f, 0.22f, -0.22f, -0.22f, 0.22f, 0.22f, 0.22f };
constexpr GLfloat kLines[]    = { -0.22f, -0.22f, 0.22f, 0.22f,  -0.22f, 0.22f, 0.22f, -0.22f };
constexpr GLfloat kPoints[]   = { 0.00f, 0.00f,  0.00f, 0.22f,  0.00f, -0.22f,  0.22f, 0.00f, -0.22f, 0.00f };

} // namespace

ShapePass::ShapePass(const char* name, SharedState* state,
                     GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b,
                     GLenum primitive, std::span<const GLfloat> verts)
    : gldx::RenderPass(name), state_(state),
      cx_(cx), cy_(cy), cr_(r), cg_(g), cb_(b),
      primitive_(primitive), vertexCount_(static_cast<GLsizei>(verts.size() / 2)) {
    auto program = gldx::ShaderProgram::CreateFromSource(kShapeVert, kShapeFrag);
    if (!program) {
        std::fprintf(stderr, "render_passes: %s program failed: %s\n",
                     name, program.error().c_str());
        return;   // program_ stays null; Execute() no-ops, the run still ends clean
    }
    program_ = std::make_unique<gldx::ShaderProgram>(std::move(*program));

    vbo_.Create(GL_ARRAY_BUFFER, verts);
    vao_.Create();
    vao_.Bind();
    vbo_.Bind(GL_ARRAY_BUFFER);
    vao_.AttachAttribute(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), nullptr);
    vao_.Unbind();
}

void ShapePass::Execute(gldx::RenderFrame& frame) {
    if (!program_) return;
    const double phase = state_ ? state_->phase.load(std::memory_order_relaxed) : 0.0;
    const float  aspect = frame.fbHeight > 0
                              ? static_cast<float>(frame.fbWidth) / static_cast<float>(frame.fbHeight)
                              : 1.0f;
    program_->Use();
    program_->Set("uPhase", static_cast<float>(phase));
    program_->Set("uAspect", aspect);
    program_->Set("uCx", cx_);
    program_->Set("uCy", cy_);
    program_->Set("uR", cr_);
    program_->Set("uG", cg_);
    program_->Set("uB", cb_);

    // macOS leaves GL_PROGRAM_POINT_SIZE off, so gl_PointSize is ignored unless
    // we turn it on just for the point pass and restore it after (the same
    // enable/restore posture ParticleBatch uses).
    const bool points = primitive_ == GL_POINTS;
    if (points) glEnable(GL_PROGRAM_POINT_SIZE);
    vao_.Bind();
    vao_.DrawArrays(primitive_, 0, vertexCount_);
    vao_.Unbind();
    if (points) glDisable(GL_PROGRAM_POINT_SIZE);
}

TrianglePass::TrianglePass(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b)
    : ShapePass("Triangle", s, cx, cy, r, g, b, GL_TRIANGLES, kTriangle) {}

QuadPass::QuadPass(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b)
    : ShapePass("Quad", s, cx, cy, r, g, b, GL_TRIANGLE_STRIP, kQuad) {}

LinePass::LinePass(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b)
    : ShapePass("Lines", s, cx, cy, r, g, b, GL_LINES, kLines) {}

PointPass::PointPass(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b)
    : ShapePass("Points", s, cx, cy, r, g, b, GL_POINTS, kPoints) {}

void ClearPass::Execute(gldx::RenderFrame& frame) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, frame.fbWidth, frame.fbHeight);
    glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

namespace {

// Free factories so the table can hold plain function pointers to the concrete
// subclasses. Each returns a *different* ShapePass type.
std::unique_ptr<ShapePass> makeTriangle(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b) {
    return std::make_unique<TrianglePass>(s, cx, cy, r, g, b);
}
std::unique_ptr<ShapePass> makeQuad(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b) {
    return std::make_unique<QuadPass>(s, cx, cy, r, g, b);
}
std::unique_ptr<ShapePass> makeLines(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b) {
    return std::make_unique<LinePass>(s, cx, cy, r, g, b);
}
std::unique_ptr<ShapePass> makePoints(SharedState* s, GLfloat cx, GLfloat cy, GLfloat r, GLfloat g, GLfloat b) {
    return std::make_unique<PointPass>(s, cx, cy, r, g, b);
}

} // namespace

const std::vector<ShapeSpec>& ShapeTable() {
    // One row per shape: an NDC slot, a colour, and which subclass to build. The
    // View loops this and AddPass()es each - the for-loop-over-different-passes
    // the demo is built to demonstrate. Rotation direction alternates by phase
    // offset baked into the shared clock (all windows spin together).
    static const std::vector<ShapeSpec> table = {
        {"triangle", -0.60f,  0.35f, 0.95f, 0.45f, 0.35f, &makeTriangle},
        {"quad",      0.00f,  0.35f, 0.40f, 0.85f, 0.55f, &makeQuad},
        {"lines",     0.60f,  0.35f, 0.45f, 0.60f, 0.95f, &makeLines},
        {"points",   -0.30f, -0.40f, 0.95f, 0.80f, 0.35f, &makePoints},
        {"triangle2", 0.30f, -0.40f, 0.75f, 0.45f, 0.90f, &makeTriangle},
    };
    return table;
}

} // namespace render_passes
