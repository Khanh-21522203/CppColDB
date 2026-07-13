#pragma once
#include <mutex>

#include "cppcoldb/engine/abstractions/checkpoint/i_checkpoint_manager.hpp"
#include "cppcoldb/engine/abstractions/scheduler/i_task.hpp"
#include "cppcoldb/engine/abstractions/scheduler/i_task_scheduler.hpp"
#include "cppcoldb/engine/abstractions/storage/i_block_manager.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_manager.hpp"
#include "cppcoldb/engine/abstractions/storage/i_wal.hpp"

namespace cppcoldb::engine::catalog { class Catalog; } // owned by another engine domain

namespace cppcoldb::engine::checkpoint {

// Coordinates a consistent checkpoint: flushes dirty buffer pool pages, then
// truncates the WAL up to the checkpoint marker.
class CheckpointManager final : public ICheckpointManager {
public:
    CheckpointManager(cppcoldb::engine::catalog::Catalog& catalog,
                       cppcoldb::engine::storage::IBufferManager& bm,
                       cppcoldb::engine::storage::IWal& wal,
                       cppcoldb::engine::storage::IBlockManager& block_file);
    ~CheckpointManager() override = default;

    bool CreateCheckpoint() override;
    void ScheduleAsyncCheckpoint(cppcoldb::engine::scheduler::ITaskScheduler& scheduler) override;

    bool IsCheckpointing() const override { return is_checkpointing_; }

private:
    cppcoldb::engine::catalog::Catalog&        catalog_;
    cppcoldb::engine::storage::IBufferManager& bm_;
    cppcoldb::engine::storage::IWal&           wal_;
    cppcoldb::engine::storage::IBlockManager&  block_file_;

    std::mutex checkpoint_lock_;
    bool       is_checkpointing_ = false;
};

// Adapts a checkpoint run to the scheduler's task abstraction.
class AsyncCheckpointTask final : public cppcoldb::engine::scheduler::ITask {
public:
    explicit AsyncCheckpointTask(CheckpointManager& mgr) : mgr_(mgr) {}
    ~AsyncCheckpointTask() override = default;

    TaskResult Execute() override;

private:
    CheckpointManager& mgr_;
};

} // namespace cppcoldb::engine::checkpoint
