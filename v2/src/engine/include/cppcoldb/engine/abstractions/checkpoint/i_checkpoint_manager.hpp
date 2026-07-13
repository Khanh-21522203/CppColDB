#pragma once

#include "cppcoldb/engine/abstractions/scheduler/i_task_scheduler.hpp"

namespace cppcoldb::engine::checkpoint {

// Coordinates a consistent checkpoint: flushes dirty buffer pool pages to the
// backing store, then truncates the WAL up to the checkpoint marker.
class ICheckpointManager {
public:
    virtual ~ICheckpointManager() = default;

    // Run a full checkpoint synchronously.
    // Returns true if the checkpoint ran, false if skipped (already running).
    virtual bool CreateCheckpoint() = 0;

    // Submit an async checkpoint to the given scheduler.
    virtual void ScheduleAsyncCheckpoint(cppcoldb::engine::scheduler::ITaskScheduler& scheduler) = 0;

    virtual bool IsCheckpointing() const = 0;
};

} // namespace cppcoldb::engine::checkpoint
