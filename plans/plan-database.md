# Feature: Database

## 1. Purpose

The `Database` class is the top-level object that owns and initializes all subsystems of CppColDB. It is the single entry point for embedding the database in a program or REPL. It handles two modes: in-memory (no file path, no WAL, no checkpoint) and persistent (file path provided). On startup from an existing file, it replays the WAL for crash recovery. On shutdown (destructor), it safely closes all connections, flushes the WAL, optionally checkpoints, and releases all memory.

## 2. Responsibilities

- Construct all subsystems in dependency order: `BufferManager` → `WAL` → `Catalog` → `TransactionManager` → `CheckpointManager` → `TaskScheduler`
- `Open(path, config)` (persistent mode): open or create the `BlockFile`; if an existing checkpoint block is present, deserialize the `Catalog` and `RowGroup` metadata; if a WAL file exists, replay it; re-checkpoint and truncate WAL after replay
- `Open(":memory:", config)` (in-memory mode): initialize empty subsystems; skip all file I/O
- `Connect()`: create and return a `Connection` object backed by this `Database`
- Destructor: roll back active transactions; flush WAL; optionally create a final checkpoint; release `BufferManager`; stop `TaskScheduler`; destroy subsystems in reverse order
- Expose subsystem accessors (`GetCatalog()`, `GetTransactionManager()`, etc.) for use by `Connection` and `ClientContext`

## 3. Non-Responsibilities

- Does not parse or execute SQL (that is `Connection` / `ClientContext`)
- Does not manage user authentication or permissions
- Does not handle network connections
- Does not support multiple database files (single-file model)
- Does not run as a server process

## 4. Architecture Design

```
main() or embedding application
    |
    v
Database db("path/to/db.file", config)
    |
    +-- BufferManager  (memory pool, BlockFile)
    +-- WAL            (write-ahead log file)
    +-- Catalog        (schema registry, MVCC-aware)
    +-- TransactionManager  (tx lifecycle, WAL lock)
    +-- CheckpointManager   (coordinates checkpoint)
    +-- TaskScheduler       (background thread pool)
    |
    v
db.Connect()
    |
    v
Connection conn(db)
    |
    v
conn.Query("SELECT ...")
    |
    v
ClientContext (per-query state: Parser, Binder, Optimizer, Executor)
```

**Initialization order:**
```
1. Config validation
2. BufferManager (needs config: pool_size, block_size, file_path)
3. WAL (needs file path; or null for :memory:)
4. Catalog (empty; or deserialized from checkpoint block)
5. TransactionManager (needs Catalog, WAL, CheckpointManager, TaskScheduler)
6. CheckpointManager (needs Catalog, BufferManager, WAL, StorageManager)
7. TaskScheduler (background thread pool)
8. WAL replay (if persistent and WAL file exists)
9. Re-checkpoint after replay
```

## 5. Core Data Structures (C++)

```cpp
// src/main/database.hpp
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
class Connection;

struct DatabaseConfig {
    // Memory pool size in bytes (applies to persistent and in-memory modes).
    size_t buffer_pool_bytes = 256ULL * 1024 * 1024; // 256 MiB

    // Block size in bytes (column data and catalog header block size).
    size_t block_size = 256 * 1024; // 256 KiB

    // WAL size threshold to trigger automatic async checkpoint.
    size_t checkpoint_threshold_bytes = 64 * 1024 * 1024; // 64 MiB

    // Number of background worker threads for TaskScheduler.
    size_t task_scheduler_threads = 1;

    // Maximum number of concurrent connections (0 = unlimited).
    size_t max_connections = 0;
};

class Database {
public:
    // Open a persistent database at 'path'.
    // Use path = ":memory:" for an in-memory database.
    // Throws IOError if the file cannot be opened or WAL replay fails.
    explicit Database(const std::string& path,
                      DatabaseConfig config = {});

    // Destructor: safely shuts down all subsystems.
    ~Database();

    // Create a new connection to this database.
    std::unique_ptr<Connection> Connect();

    // Accessors for subsystems (used by Connection and ClientContext).
    BufferManager&      GetBufferManager()     { return *buffer_manager_; }
    WAL&                GetWAL()               { return *wal_; }
    Catalog&            GetCatalog()           { return *catalog_; }
    TransactionManager& GetTransactionManager(){ return *txn_manager_; }
    CheckpointManager&  GetCheckpointManager() { return *ckpt_manager_; }
    TaskScheduler&      GetTaskScheduler()     { return *scheduler_; }

    bool IsInMemory()   const { return is_in_memory_; }
    const std::string& Path() const { return path_; }

private:
    void InitInMemory();
    void InitPersistent(const std::string& path);
    void LoadFromCheckpoint();
    void ReplayWAL();
    void Shutdown();

    std::string    path_;
    bool           is_in_memory_;
    DatabaseConfig config_;

    // Owned subsystems (in construction order).
    std::unique_ptr<BufferManager>     buffer_manager_;
    std::unique_ptr<WAL>               wal_;
    std::unique_ptr<Catalog>           catalog_;
    std::unique_ptr<TransactionManager> txn_manager_;
    std::unique_ptr<CheckpointManager> ckpt_manager_;
    std::unique_ptr<TaskScheduler>     scheduler_;

    // Open connections (weak_ptrs; connections are owned by the caller).
    mutable std::mutex          connections_mu_;
    std::vector<Connection*>    open_connections_;
};

} // namespace cppcoldb
```

```cpp
// src/main/connection.hpp
#pragma once
#include <memory>
#include <string>
#include "main/client_context.hpp"

namespace cppcoldb {

class Database;
class Transaction;

class Connection {
public:
    explicit Connection(Database& db);
    ~Connection(); // rolls back any open transaction, deregisters from Database

    // Execute a SQL statement; returns a QueryResult.
    QueryResult Query(const std::string& sql);

    // Explicit transaction control (used when auto_commit == false).
    void Begin();
    void Commit();
    void Rollback();

    Database& GetDatabase() { return db_; }

private:
    Database&                     db_;
    ClientContext                 ctx_;
    std::shared_ptr<Transaction>  active_transaction_; // nullptr if no explicit txn
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class Database {
public:
    explicit Database(const std::string& path, DatabaseConfig config = {});
    ~Database();

    std::unique_ptr<Connection> Connect();

    BufferManager&      GetBufferManager();
    WAL&                GetWAL();
    Catalog&            GetCatalog();
    TransactionManager& GetTransactionManager();
    CheckpointManager&  GetCheckpointManager();
    TaskScheduler&      GetTaskScheduler();

    bool               IsInMemory() const;
    const std::string& Path()       const;
};

class Connection {
public:
    explicit Connection(Database& db);
    ~Connection();

    QueryResult Query(const std::string& sql);
    void        Begin();
    void        Commit();
    void        Rollback();
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### Database initialization (persistent mode)
```
InitPersistent(path):
  1. Open or create BlockFile at path
  2. buffer_manager_ = new BufferManager(config.pool_bytes, config.block_size, block_file)
  3. wal_path = path + ".wal"
  4. wal_ = WAL::Create(wal_path)          // or OpenForReplay if exists
  5. catalog_ = new Catalog()
  6. txn_manager_ = new TransactionManager(catalog_, wal_, ckpt_manager_, scheduler_)
  7. ckpt_manager_ = new CheckpointManager(catalog_, buffer_manager_, wal_, ...)
  8. scheduler_ = new TaskScheduler(config.task_scheduler_threads)

  if BlockFile has checkpoint block (block 0 non-empty):
    LoadFromCheckpoint()             // deserialize catalog + row group metadata

  if WAL file exists AND not empty:
    ReplayWAL()                      // replay entries past last checkpoint
    ckpt_manager_.CreateCheckpoint() // re-checkpoint recovered state
    wal_.Truncate()                  // clean up replayed entries
```

### Database initialization (in-memory mode)
```
InitInMemory():
  buffer_manager_ = new BufferManager(config.pool_bytes, config.block_size, nullptr)
  wal_            = nullptr       // no WAL in memory mode
  catalog_        = new Catalog()
  txn_manager_    = new TransactionManager(catalog_, null_wal, ...)
  ckpt_manager_   = nullptr       // no checkpointing
  scheduler_      = new TaskScheduler(config.task_scheduler_threads)
```

### Database shutdown (destructor)
```
~Database():
  1. Signal TaskScheduler to stop accepting new tasks; wait for running tasks to complete
  2. For each open connection:
       if connection has active transaction:
         txn_manager_.Rollback(connection.active_transaction)
  3. If not in-memory AND wal_ has pending entries:
       wal_.Flush()
  4. If not in-memory AND wal_.FileSizeBytes() > threshold:
       ckpt_manager_.CreateCheckpoint()   // final checkpoint on clean shutdown
  5. buffer_manager_.Shutdown()           // flush dirty blocks, close BlockFile
  6. wal_.Close()
  7. scheduler_.Shutdown()
  8. Destroy subsystems in reverse construction order
```

### WAL replay
```
ReplayWAL():
  wal_.OpenForReplay()
  last_checkpoint_seen = false
  while wal_.ReadNextEntry(entry):
    if entry.type == WAL_CHECKPOINT:
      last_checkpoint_seen = true
      // mark position; entries before this are already in checkpoint
      continue
    if not last_checkpoint_seen:
      continue  // skip entries before last checkpoint (already applied)

    switch entry.type:
      WAL_CREATE_TABLE: catalog_.CreateTable(...)
      WAL_DROP_TABLE:   catalog_.DropTable(...)
      WAL_INSERT:       replay rows into RowGroup storage
      WAL_DELETE:       apply delete markers
      WAL_UPDATE:       apply updated values
  // after replay: all committed state is restored in memory
```

## 8. Persistence Model

```
File layout on disk:
  <path>          — main BlockFile (column data blocks + catalog header at block 0)
  <path>.wal      — WAL file (sequential binary log of committed changes)

On clean shutdown:
  A checkpoint is performed if the WAL is non-empty, leaving the system
  recoverable from block 0 alone with an empty or absent WAL.

On crash:
  At next startup, block 0 has the last checkpointed state;
  the .wal file contains entries since the last checkpoint;
  WAL replay restores any committed transactions not yet in block 0.
```

## 9. Concurrency Model

| Object | Lock | Usage |
|--------|------|-------|
| `Database.connections_mu_` | `std::mutex` | Guard `open_connections_` during Connect/~Connection |
| All subsystems | Own internal locks | See individual subsystem plans |

Multiple `Connection` objects can be created from the same `Database` and used concurrently. Each `Connection` owns its own `ClientContext` and `Transaction`. The subsystems (`Catalog`, `TransactionManager`, `BufferManager`) are each thread-safe for concurrent use by multiple connections.

## 10. Configuration

```cpp
struct DatabaseConfig {
    size_t buffer_pool_bytes           = 256ULL * 1024 * 1024; // 256 MiB
    size_t block_size                  = 256 * 1024;           // 256 KiB
    size_t checkpoint_threshold_bytes  = 64 * 1024 * 1024;     // 64 MiB
    size_t task_scheduler_threads      = 1;
    size_t max_connections             = 0; // 0 = unlimited
};
```

## 11. Testing Strategy

- `TestInMemoryDatabase`: open with ":memory:", create table, insert, query → correct results, no files created
- `TestPersistentDatabaseCreate`: open new file, create table, close → file exists
- `TestPersistentDatabaseReopen`: create table in session 1, close; reopen in session 2 → table visible
- `TestWALReplayOnCrash`: write data, simulate crash (no checkpoint), reopen → data recovered via WAL
- `TestCheckpointOnShutdown`: insert rows, close database (clean) → WAL truncated, checkpoint block valid
- `TestMultipleConnections`: two connections to same database, concurrent inserts → both committed correctly
- `TestConnectionRollbackOnClose`: open connection, BEGIN, insert, close without commit → rows gone
- `TestShutdownWithOpenTransactions`: open connection with active txn, destroy Database → txn rolled back
- `TestInMemoryNoFiles`: verify no files are created for in-memory database
- `TestConfigRespected`: open with small pool_bytes → OutOfMemory when pool is exhausted

## 12. Open Questions

None.
