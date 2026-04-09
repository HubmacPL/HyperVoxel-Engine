#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t numThreads) {
    workers_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock lock(mutex_);
                    cv_.wait(lock, [this] {
                        return stop_.load(std::memory_order_relaxed) || !tasks_.empty();
                    });
                    if (stop_.load(std::memory_order_relaxed) && tasks_.empty())
                        return;
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                activeTasks_.fetch_add(1, std::memory_order_relaxed);
                task();
                activeTasks_.fetch_sub(1, std::memory_order_relaxed);
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true, std::memory_order_relaxed);
    cv_.notify_all();
    for (auto& w : workers_) w.join();
}

void ThreadPool::waitAll() {
    while (pendingTasks() > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
