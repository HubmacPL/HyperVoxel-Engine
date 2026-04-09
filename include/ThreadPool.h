#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <future>
#include <vector>
#include <atomic>
#include <type_traits>

// ─────────────────────────────────────────────────────────────────────────────
//  ThreadPool — fixed-size worker pool for background chunk operations
//
//  Usage:
//    ThreadPool pool(std::thread::hardware_concurrency() - 1);
//    auto fut = pool.submit([&]{ return buildMesh(ctx); });
//    ChunkMesh mesh = fut.get();
// ─────────────────────────────────────────────────────────────────────────────
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a callable, returns a future for the result
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using RetT = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<RetT()>>(
            [f = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable {
                return f(std::forward<Args>(args)...);
            }
        );
        std::future<RetT> fut = task->get_future();
        {
            std::unique_lock lock(mutex_);
            if (stop_) throw std::runtime_error("ThreadPool is stopped");
            tasks_.emplace([task]{ (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    // Number of tasks waiting + running
    [[nodiscard]] size_t pendingTasks() const noexcept {
        std::unique_lock lock(mutex_);
        return tasks_.size() + activeTasks_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t threadCount() const noexcept { return workers_.size(); }

    void waitAll();

private:
    std::vector<std::thread>         workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex               mutex_;
    std::condition_variable          cv_;
    std::atomic<bool>                stop_{false};
    std::atomic<size_t>              activeTasks_{0};
};
