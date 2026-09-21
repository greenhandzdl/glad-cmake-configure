module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

Profiler::~Profiler() {
    if (queries_[0] != 0 || queries_[1] != 0) {
        RenderContext::AssertRenderThread("~Profiler");
        glDeleteQueries(2, queries_);
    }
}

void Profiler::Init() {
    RenderContext::AssertRenderThread("Profiler::Init");
    if (!ready_) {
        glGenQueries(2, queries_);
        ready_ = true;
    }
}

void Profiler::BeginFrame() {
    RenderContext::AssertRenderThread("Profiler::BeginFrame");
    cpuStart_ = std::chrono::steady_clock::now();

    if (!ready_) return;
    // Harvest the previous frame's query (the buffer we are NOT about to use).
    const GLuint prev = queries_[cur_ ^ 1];
    GLint available = 0;
    glGetQueryObjectiv(prev, GL_QUERY_RESULT_AVAILABLE, &available);
    if (available) {
        GLuint64 elapsedNs = 0;
        glGetQueryObjectui64v(prev, GL_QUERY_RESULT, &elapsedNs);
        const float ms = static_cast<float>(elapsedNs) / 1.0e6f;
        gpuMs_ += (ms - gpuMs_) * kSmoothing;
    }
    // Start timing this frame into the current buffer.
    glBeginQuery(GL_TIME_ELAPSED, queries_[cur_]);
}

void Profiler::EndFrame() {
    RenderContext::AssertRenderThread("Profiler::EndFrame");
    if (ready_) glEndQuery(GL_TIME_ELAPSED);

    const auto now = std::chrono::steady_clock::now();
    const float ms = std::chrono::duration<float, std::milli>(now - cpuStart_).count();
    cpuMs_ += (ms - cpuMs_) * kSmoothing;

    cur_ ^= 1;
}

} // namespace gldx
