#include "checkpoint/checkpoint_manager.hpp"
#include "catalog/catalog.hpp"
#include "storage/buffer_manager.hpp"
#include "storage/block_file.hpp"
#include "storage/wal.hpp"
#include "parallel/task_scheduler.hpp"
#include <memory>

namespace cppcoldb {

CheckpointManager::CheckpointManager(Catalog& catalog, BufferManager& bm,
                                     WAL& wal, BlockFile& block_file)
    : catalog_(catalog), bm_(bm), wal_(wal), block_file_(block_file) {}

bool CheckpointManager::CreateCheckpoint() {
    // Try to acquire the checkpoint lock; skip if another checkpoint is running.
    std::unique_lock<std::mutex> lk(checkpoint_lock_, std::try_to_lock);
    if (!lk.owns_lock()) {
        return false;
    }

    is_checkpointing_ = true;

    // Write WAL checkpoint marker and flush before touching block storage.
    wal_.WriteCheckpointMarker();
    wal_.Flush();

    // Pin block 0 (catalog header). Allocate it if this is the first checkpoint.
    BufferHandle handle;
    if (block_file_.BlockCount() == 0) {
        handle = bm_.AllocateBlock();
    } else {
        handle = bm_.Pin(0);
    }

    // Serialize catalog metadata into the block buffer.
    catalog_.Serialize(handle.Data(), bm_.BlockSize());

    // Mark dirty so BufferManager writes it back on Flush.
    handle.MarkDirty();

    // Flush all dirty blocks to the BlockFile and fsync.
    bm_.Flush();

    // Truncate the WAL — entries before the last checkpoint marker are now safe to drop.
    wal_.Truncate();

    is_checkpointing_ = false;
    return true;
}

void CheckpointManager::ScheduleAsyncCheckpoint(TaskScheduler& scheduler) {
    scheduler.Submit(std::make_unique<AsyncCheckpointTask>(*this));
}

} // namespace cppcoldb
