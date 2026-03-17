#pragma once

namespace cppcoldb {

class Task {
public:
    enum class TaskResult { FINISHED, ERROR };
    virtual TaskResult Execute() = 0;
    virtual ~Task() = default;
};

} // namespace cppcoldb
