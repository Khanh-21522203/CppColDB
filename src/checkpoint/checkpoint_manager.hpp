#pragma once
#include <mutex>
#include "parallel/task.hpp"

namespace cppcoldb {

class Catalog;
class BufferManager;
class WAL;
class BlockFile;
class TaskScheduler;

class CheckpointManager {
public:
    CheckpointManager(Catalog& catalog, BufferManager& bm, WAL& wal, BlockFile& block_file);

    // Run a full checkpoint synchronously.
    // Returns true if checkpoint ran, false if skipped (already running).
    bool CreateCheckpoint();

    // Submit an async checkpoint to the scheduler.
    void ScheduleAsyncCheckpoint(TaskScheduler& scheduler);

    bool IsCheckpointing() const { return is_checkpointing_; }

private:
    Catalog&       catalog_;
    BufferManager& bm_;
    WAL&           wal_;
    BlockFile&     block_file_;

    std::mutex checkpoint_lock_;
    bool       is_checkpointing_ = false;
};

class AsyncCheckpointTask : public Task {
public:
    explicit AsyncCheckpointTask(CheckpointManager& mgr) : mgr_(mgr) {}
    TaskResult Execute() override {
        mgr_.CreateCheckpoint();
        return TaskResult::FINISHED;
    }
private:
    CheckpointManager& mgr_;
};

} // namespace cppcoldb
