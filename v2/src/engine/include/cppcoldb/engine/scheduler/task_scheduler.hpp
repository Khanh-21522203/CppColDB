#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cppcoldb/engine/abstractions/scheduler/i_task.hpp"
#include "cppcoldb/engine/abstractions/scheduler/i_task_scheduler.hpp"

namespace cppcoldb::engine::scheduler {

// Fixed-size thread pool executing submitted ITask instances FIFO.
class TaskScheduler final : public ITaskScheduler {
public:
    explicit TaskScheduler(std::size_t num_threads = 1);
    ~TaskScheduler() override = default;

    // Spawn worker threads. Must be called before Submit().
    void Initialize() override;

    // Enqueue a task for background execution (transfers ownership).
    // Throws RuntimeError if called after Shutdown().
    void Submit(std::unique_ptr<ITask> task) override;

    // Stop accepting new tasks; wait for all running tasks to complete.
    void Shutdown() override;

    bool IsShutdown() const override { return shutdown_.load(std::memory_order_acquire); }

private:
    void WorkerLoop();

    std::size_t                        num_threads_;
    std::vector<std::thread>           workers_;
    std::deque<std::unique_ptr<ITask>> queue_;
    std::mutex                         mu_;
    std::condition_variable            cv_;
    std::atomic<bool>                  shutdown_{false};
    bool                                initialized_ = false;
};

} // namespace cppcoldb::engine::scheduler
