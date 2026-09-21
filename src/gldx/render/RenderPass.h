#ifndef GLDX_RENDER_RENDERPASS_H
#define GLDX_RENDER_RENDERPASS_H

/**
 * @file RenderPass.h
 * @brief Base class for one named stage of the frame (plan "RenderPass").
 *
 * A pass reads/writes the shared RenderFrame and issues GL on the render thread.
 * The Renderer keeps an ordered vector of passes and runs them in sequence - a
 * deliberately simple linear pipeline rather than a full dependency/transient
 * render graph: with a fixed, small set of stages this ordering is easier to
 * reason about and costs nothing, while still decoupling each stage's GL work
 * from the application loop. (The rationale is captured in doc/design.md.)
 */

namespace gldx {

struct RenderFrame;

class RenderPass {
public:
    explicit RenderPass(const char* name) noexcept : name_(name) {}
    virtual ~RenderPass() = default;

    RenderPass(const RenderPass&)            = delete;
    RenderPass& operator=(const RenderPass&) = delete;

    virtual void Execute(RenderFrame& frame) = 0;

    [[nodiscard]] const char* name() const noexcept { return name_; }

private:
    const char* name_;
};

} // namespace gldx

#endif // GLDX_RENDER_RENDERPASS_H
