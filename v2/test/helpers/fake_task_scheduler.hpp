#pragma once
#include <memory>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/scheduler/i_task_scheduler.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for ITaskScheduler. Not executed; every method throws.
class FakeTaskScheduler final : public engine::scheduler::ITaskScheduler {
public:
    void Initialize() override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void Submit(std::unique_ptr<engine::scheduler::ITask> task) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void Shutdown() override { CPPCOLDB_NOT_IMPLEMENTED(); }
    bool IsShutdown() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
