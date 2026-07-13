#pragma once

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/checkpoint/i_checkpoint_manager.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for ICheckpointManager. Not executed; every method throws.
class FakeCheckpointManager final : public engine::checkpoint::ICheckpointManager {
public:
    bool CreateCheckpoint() override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void ScheduleAsyncCheckpoint(engine::scheduler::ITaskScheduler& scheduler) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    bool IsCheckpointing() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
