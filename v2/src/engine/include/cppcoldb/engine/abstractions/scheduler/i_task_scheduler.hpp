#pragma once
#include <memory>

#include "cppcoldb/engine/abstractions/scheduler/i_task.hpp"

namespace cppcoldb::engine::scheduler {

// Runs submitted ITask instances on a background worker pool.
class ITaskScheduler {
public:
    virtual ~ITaskScheduler() = default;

    // Spawn worker threads. Must be called before Submit().
    virtual void Initialize() = 0;

    // Enqueue a task for background execution (transfers ownership).
    // Throws RuntimeError if called after Shutdown().
    virtual void Submit(std::unique_ptr<ITask> task) = 0;

    // Stop accepting new tasks; wait for all running tasks to complete.
    virtual void Shutdown() = 0;

    virtual bool IsShutdown() const = 0;
};

} // namespace cppcoldb::engine::scheduler
