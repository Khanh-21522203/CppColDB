#include "cppcoldb/engine/checkpoint/checkpoint_manager.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::checkpoint {

CheckpointManager::CheckpointManager(cppcoldb::engine::catalog::Catalog& catalog,
                                      cppcoldb::engine::storage::IBufferManager& bm,
                                      cppcoldb::engine::storage::IWal& wal,
                                      cppcoldb::engine::storage::IBlockManager& block_file)
    : catalog_(catalog), bm_(bm), wal_(wal), block_file_(block_file) {}

bool CheckpointManager::CreateCheckpoint() { CPPCOLDB_NOT_IMPLEMENTED(); }

void CheckpointManager::ScheduleAsyncCheckpoint(cppcoldb::engine::scheduler::ITaskScheduler& /*scheduler*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

AsyncCheckpointTask::TaskResult AsyncCheckpointTask::Execute() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::checkpoint
