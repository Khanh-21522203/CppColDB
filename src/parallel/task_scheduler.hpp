#pragma once
#include <deque>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include "parallel/task.hpp"

namespace cppcoldb {

class TaskScheduler {
public:
    explicit TaskScheduler(size_t num_threads = 1);
    ~TaskScheduler();

    // Spawn worker threads. Must be called before Submit().
    void Initialize();

    // Enqueue a task for background execution (transfers ownership).
    // Throws RuntimeError if called after Shutdown().
    void Submit(std::unique_ptr<Task> task);

    // Stop accepting new tasks; wait for all running tasks to complete.
    void Shutdown();

    bool IsShutdown() const { return shutdown_.load(std::memory_order_acquire); }

private:
    void WorkerLoop();

    size_t                            num_threads_;
    std::vector<std::thread>          workers_;
    std::deque<std::unique_ptr<Task>> queue_;
    std::mutex                        mu_;
    std::condition_variable           cv_;
    std::atomic<bool>                 shutdown_{false};
    bool                              initialized_ = false;
};

} // namespace cppcoldb
