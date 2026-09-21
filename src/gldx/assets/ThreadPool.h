#ifndef GLDX_ASSETS_THREADPOOL_H
#define GLDX_ASSETS_THREADPOOL_H

/**
 * @file ThreadPool.h
 * @brief Minimal CPU worker pool used to run Stage-A asset work (file IO,
 *        stb decode, Assimp parsing) off the render thread (plan section 1.2).
 *
 * enqueue() is thread-safe. Tasks must not touch GL. Submitting after
 * shutdown() throws std::future_error, which main() should guard.
 */

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace gldx {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t threadCount = std::thread::hardware_concurrency());
    ~ThreadPool();

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Enqueue a callable and get a future for its result.
    template <class F>
    auto enqueue(F&& f) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (stop_) throw std::future_error(std::future_errc::broken_promise);
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    [[nodiscard]] std::size_t threadCount() const noexcept { return workers_.size(); }

    // Signal workers to drain and exit; joins all threads. Idempotent.
    void shutdown() noexcept;

private:
    void WorkerMain();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

} // namespace gldx

#endif // GLDX_ASSETS_THREADPOOL_H
