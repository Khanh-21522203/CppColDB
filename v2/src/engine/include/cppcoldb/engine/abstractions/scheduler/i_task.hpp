#pragma once

namespace cppcoldb::engine::scheduler {

// Unit of work executed by an ITaskScheduler worker thread.
class ITask {
public:
    enum class TaskResult { FINISHED, ERROR };

    virtual ~ITask() = default;
    virtual TaskResult Execute() = 0;
};

} // namespace cppcoldb::engine::scheduler
