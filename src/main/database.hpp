#pragma once
#include <string>
#include <memory>
#include <vector>
#include <mutex>

namespace cppcoldb {

class BufferManager;
class WAL;
class Catalog;
class TransactionManager;
class CheckpointManager;
class TaskScheduler;
class BlockFile;
class Connection;

struct DatabaseConfig {
    size_t buffer_pool_bytes          = 256ULL * 1024 * 1024; // 256 MiB
    size_t block_size                 = 256 * 1024;           // 256 KiB
    size_t checkpoint_threshold_bytes = 64 * 1024 * 1024;     // 64 MiB
    size_t task_scheduler_threads     = 1;
};

class Database {
public:
    explicit Database(const std::string& path, DatabaseConfig config = {});
    ~Database();

    std::unique_ptr<Connection> Connect();

    // Subsystem accessors.
    BufferManager&      GetBufferManager();
    WAL&                GetWAL();
    Catalog&            GetCatalog();
    TransactionManager& GetTransactionManager();
    TaskScheduler&      GetTaskScheduler();

    bool               IsInMemory() const { return is_in_memory_; }
    const std::string& Path()       const { return path_; }

    // Called by Connection constructor/destructor.
    void RegisterConnection(Connection* conn);
    void UnregisterConnection(Connection* conn);

private:
    void InitInMemory();
    void InitPersistent(const std::string& path);
    void ReplayWAL();
    void Shutdown();

    std::string    path_;
    bool           is_in_memory_;
    DatabaseConfig config_;

    std::unique_ptr<BlockFile>          block_file_;    // null in memory mode
    std::unique_ptr<BufferManager>      buffer_manager_;
    std::unique_ptr<WAL>                wal_;           // null in memory mode
    std::unique_ptr<Catalog>            catalog_;
    std::unique_ptr<TransactionManager> txn_manager_;
    std::unique_ptr<CheckpointManager>  ckpt_manager_;  // null in memory mode
    std::unique_ptr<TaskScheduler>      scheduler_;

    mutable std::mutex       connections_mu_;
    std::vector<Connection*> open_connections_;
};

} // namespace cppcoldb
