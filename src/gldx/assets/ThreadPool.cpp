module;

#include "gldx/gmf.hpp"

module gldx;

namespace gldx {

ThreadPool::ThreadPool(std::size_t threadCount) {
    if (threadCount == 0) threadCount = 1;
    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] { WorkerMain(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::WorkerMain() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) return;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try {
            task();
        } catch (...) {
            // A throwing task must not escape the worker thread's top level —
            // an exception leaving a std::thread calls std::terminate and aborts
            // the whole process. Drop it; any linked std::future is delivered as
            // broken_promise, which the consumer on the render thread handles.
        }
    }
}

void ThreadPool::shutdown() noexcept {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (stop_) return;
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

} // namespace gldx
