#pragma once
#include <memory>
#include <string>

#include "cppcoldb/database_config.hpp"
#include "cppcoldb/engine/abstractions/catalog/i_catalog.hpp"
#include "cppcoldb/engine/abstractions/checkpoint/i_checkpoint_manager.hpp"
#include "cppcoldb/engine/abstractions/io/i_clock.hpp"
#include "cppcoldb/engine/abstractions/io/i_file_system.hpp"
#include "cppcoldb/engine/abstractions/io/i_logger.hpp"
#include "cppcoldb/engine/abstractions/scheduler/i_task_scheduler.hpp"
#include "cppcoldb/engine/abstractions/storage/i_block_manager.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_manager.hpp"
#include "cppcoldb/engine/abstractions/storage/i_wal.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction_manager.hpp"

namespace cppcoldb {

class Connection;

// Composition root of the database. Owns every engine subsystem via its
// abstract interface (never the concrete implementation type) plus the
// infrastructure I/O ports those subsystems are wired against, and hands out
// per-session Connections.
class Database {
public:
    explicit Database(const std::string& path, DatabaseConfig config = {});
    ~Database();

    Database(const Database&)            = delete;
    Database& operator=(const Database&) = delete;

    // Open a new session against this database.
    std::unique_ptr<Connection> Connect();

    bool               IsInMemory() const { return config_.in_memory; }
    const std::string& Path()      const { return path_; }

private:
    std::string    path_;
    DatabaseConfig config_;

    // Engine subsystems — interface types, wired to concrete implementations
    // in the .cpp constructor.
    std::unique_ptr<engine::storage::IBufferManager>          buffer_manager_;
    std::unique_ptr<engine::storage::IBlockManager>           block_manager_;
    std::unique_ptr<engine::storage::IWal>                    wal_;
    std::unique_ptr<engine::catalog::ICatalog>                catalog_;
    std::unique_ptr<engine::transaction::ITransactionManager> transaction_manager_;
    std::unique_ptr<engine::checkpoint::ICheckpointManager>   checkpoint_manager_;
    std::unique_ptr<engine::scheduler::ITaskScheduler>        task_scheduler_;

    // Infrastructure adapters, held behind their engine::io ports.
    std::unique_ptr<engine::io::IFileSystem> file_system_;
    std::unique_ptr<engine::io::IClock>      clock_;
    std::unique_ptr<engine::io::ILogger>     logger_;
};

} // namespace cppcoldb
