# Feature: Transaction

## 1. Purpose

The Transaction subsystem provides MVCC snapshot isolation for all database operations. Each transaction is assigned a monotonically increasing `tx_id` and a `start_time` (snapshot timestamp). The transaction sees all data committed before its `start_time` and its own uncommitted changes. All local changes are buffered in an `UndoBuffer` until commit (where they are applied to main storage and the WAL is written) or rollback (where they are reversed in LIFO order). The `TransactionManager` is the single authority for creating, committing, and rolling back transactions, and for running MVCC garbage collection.

## 2. Responsibilities

- `TransactionManager::BeginTransaction(auto_commit)`: assign `tx_id` and `start_time`; create a `Transaction` object; add it to `active_transactions_`
- `TransactionManager::Commit(tx)`: write WAL, assign `commit_time`, apply `UndoBuffer` to main storage (update CatalogEntry timestamps, call `RowGroup::CommitAppend`, `VersionInfo::CommitDelete`, etc.), remove from `active_transactions_`, trigger async checkpoint if WAL is large
- `TransactionManager::Rollback(tx)`: traverse `UndoBuffer` in reverse (LIFO); undo each change; discard `TransactionLocalStorage`; remove from `active_transactions_`
- `UndoBuffer`: store entries for each change made by the transaction (CATALOG_ENTRY, INSERT_ROWS, DELETE_ROWS, UPDATE_ROWS); each entry carries enough info to undo or apply the change
- `TransactionLocalStorage`: hold un-merged INSERT rows private to the transaction; merged into `RowGroup` on commit
- MVCC garbage collection: after each commit/rollback, compute the minimum `start_time` of all active transactions; free `UndoBuffer` entries whose `commit_time` is less than that minimum

## 3. Non-Responsibilities

- Does not write column data to storage (RowGroup and ColumnChunk do that at commit)
- Does not format WAL entries (WAL class handles serialization)
- Does not manage the buffer pool
- Does not handle distributed transactions
- Does not implement optimistic concurrency control beyond write-write conflict detection

## 4. Architecture Design

```
ClientContext::Query(sql)
  |
  +-- TransactionManager::BeginTransaction(auto_commit)
  |     → tx_id = ++next_tx_id_
  |     → start_time = current_commit_counter_.load()
  |     → active_transactions_.insert(tx)
  |
  v
 (execute query; operators read via MVCC; writes go into UndoBuffer)
  |
  +-- On success (auto_commit=true):
  |     TransactionManager::Commit(tx)
  |       1. Transaction::WriteToWAL()  → WAL entries, fsync
  |       2. commit_time = ++commit_counter_
  |       3. Apply UndoBuffer → RowGroup/Catalog timestamps updated
  |       4. active_transactions_.erase(tx)
  |       5. GarbageCollect()
  |       6. If WAL large: ScheduleAsyncCheckpoint()
  |
  +-- On failure or ROLLBACK:
        TransactionManager::Rollback(tx)
          1. Traverse UndoBuffer in LIFO order
          2. Undo each entry (RevertAppend, ClearDeleteMarker, RevertUpdate, etc.)
          3. Discard TransactionLocalStorage
          4. active_transactions_.erase(tx)
          5. GarbageCollect()
```

## 5. Core Data Structures (C++)

```cpp
// src/transaction/undo_buffer.hpp
#pragma once
#include <vector>
#include <variant>
#include <memory>
#include "common/types.hpp"
#include "common/data_chunk.hpp"

namespace cppcoldb {

struct CatalogUndoEntry {
    std::string schema;
    std::string table;
    bool        was_create; // true = undo a CREATE; false = undo a DROP
};

struct InsertUndoEntry {
    std::string schema;
    std::string table;
    size_t      row_group_id;    // which RowGroup the rows were appended to
    size_t      append_start;    // first appended row offset within RowGroup
    size_t      append_count;    // number of rows appended
};

struct DeleteUndoEntry {
    std::string             schema;
    std::string             table;
    std::vector<row_t>      row_ids; // rows that were marked deleted
};

struct UpdateUndoEntry {
    std::string             schema;
    std::string             table;
    std::vector<row_t>      row_ids;
    std::vector<size_t>     col_ids;
    DataChunk               old_values; // original values before update (for rollback)
};

using UndoEntry = std::variant<CatalogUndoEntry,
                                InsertUndoEntry,
                                DeleteUndoEntry,
                                UpdateUndoEntry>;

// LIFO log of changes made by one transaction.
class UndoBuffer {
public:
    void PushCatalogEntry(CatalogUndoEntry e) { entries_.push_back(std::move(e)); }
    void PushInsert(InsertUndoEntry e)         { entries_.push_back(std::move(e)); }
    void PushDelete(DeleteUndoEntry e)         { entries_.push_back(std::move(e)); }
    void PushUpdate(UpdateUndoEntry e)         { entries_.push_back(std::move(e)); }

    // Traverse in forward (commit) order.
    void ForEachForward(std::function<void(const UndoEntry&)> fn) const;

    // Traverse in reverse (rollback) order.
    void ForEachReverse(std::function<void(const UndoEntry&)> fn) const;

    bool IsEmpty() const { return entries_.empty(); }
    void Clear()         { entries_.clear(); }

private:
    std::vector<UndoEntry> entries_;
};

} // namespace cppcoldb
```

```cpp
// src/transaction/transaction.hpp
#pragma once
#include <memory>
#include <string>
#include "common/types.hpp"
#include "transaction/undo_buffer.hpp"

namespace cppcoldb {

class WAL;
class Catalog;

class Transaction {
public:
    Transaction(TransactionId tx_id, timestamp_t start_time, bool auto_commit);

    // Serialize all UndoBuffer changes to WAL (called during commit).
    void WriteToWAL(WAL& wal) const;

    TransactionId tx_id;
    timestamp_t   start_time;      // snapshot: see data committed before this time
    timestamp_t   commit_time = INVALID_TIMESTAMP; // assigned at commit
    bool          auto_commit;
    bool          is_invalid = false; // set true after RuntimeError or IOError in explicit txn

    UndoBuffer    undo_buffer;

    // Per-table private rows waiting to be merged into RowGroup storage on commit.
    // key = "schema.table"
    std::unordered_map<std::string, DataChunk> local_storage;
};

} // namespace cppcoldb
```

```cpp
// src/transaction/transaction_manager.hpp
#pragma once
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <memory>
#include "transaction/transaction.hpp"

namespace cppcoldb {

class Catalog;
class WAL;
class CheckpointManager;
class TaskScheduler;

class TransactionManager {
public:
    TransactionManager(Catalog& catalog, WAL& wal,
                       CheckpointManager& ckpt_mgr, TaskScheduler& scheduler);

    std::shared_ptr<Transaction> BeginTransaction(bool auto_commit = true);
    void Commit(std::shared_ptr<Transaction> tx);
    void Rollback(std::shared_ptr<Transaction> tx);

    // Acquire WAL write lock (held during WAL write + fsync).
    std::unique_lock<std::mutex> AcquireWALLock();

    timestamp_t CurrentCommitTime() const {
        return commit_counter_.load(std::memory_order_acquire);
    }

private:
    void ApplyUndoBuffer(Transaction& tx);
    void GarbageCollect();

    Catalog&            catalog_;
    WAL&                wal_;
    CheckpointManager&  ckpt_mgr_;
    TaskScheduler&      scheduler_;

    std::atomic<TransactionId>  next_tx_id_      {1};
    std::atomic<timestamp_t>    commit_counter_  {1};

    mutable std::mutex mu_;
    std::unordered_map<TransactionId, std::shared_ptr<Transaction>> active_transactions_;

    std::mutex wal_write_mu_; // serializes WAL writes across concurrent transactions
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class TransactionManager {
public:
    std::shared_ptr<Transaction> BeginTransaction(bool auto_commit = true);
    void Commit(std::shared_ptr<Transaction> tx);
    void Rollback(std::shared_ptr<Transaction> tx);
    timestamp_t CurrentCommitTime() const;
};

class Transaction {
public:
    void WriteToWAL(WAL& wal) const;

    TransactionId tx_id;
    timestamp_t   start_time;
    timestamp_t   commit_time;
    bool          auto_commit;
    bool          is_invalid;
    UndoBuffer    undo_buffer;
    std::unordered_map<std::string, DataChunk> local_storage;
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### BeginTransaction
```
BeginTransaction(auto_commit):
  tx_id     = next_tx_id_.fetch_add(1)
  start_time= commit_counter_.load()   // snapshot: see all commits up to this point
  tx        = make_shared<Transaction>(tx_id, start_time, auto_commit)
  lock(mu_)
  active_transactions_[tx_id] = tx
  return tx
  Time: O(1)
```

### Commit
```
Commit(tx):
  if tx->undo_buffer.IsEmpty():
    // Read-only transaction: skip WAL, just deregister
    goto deregister

  if not in_memory:
    wal_lock = AcquireWALLock()  // serialize WAL writes
    tx->WriteToWAL(wal_)
    wal_.Flush()                 // fsync
    if flush failed: Rollback(tx); throw IOError(...)
    release wal_lock

  commit_time = ++commit_counter_

  ApplyUndoBuffer(*tx, commit_time):
    tx.undo_buffer.ForEachForward(entry:
      if CatalogUndoEntry:
        catalog_.CommitEntry(schema, table, commit_time)
      if InsertUndoEntry:
        // Merge local_storage rows into RowGroup
        table = catalog_.GetTable(schema, table, *tx)
        table.storage.AppendFromLocalStorage(tx->local_storage["schema.table"])
        rg.CommitAppend(commit_time)
      if DeleteUndoEntry:
        for row_id in entry.row_ids:
          rg.version_info.CommitDelete(row_offset, commit_time)
      if UpdateUndoEntry:
        for row_id in entry.row_ids:
          rg.version_info.CommitUpdate(row_offset, commit_time)
    )

deregister:
  lock(mu_)
  active_transactions_.erase(tx->tx_id)
  GarbageCollect()

  if wal_.FileSizeBytes() > CHECKPOINT_THRESHOLD:
    ckpt_mgr_.ScheduleAsyncCheckpoint(scheduler_)
  Time: O(undo_entries + rows_modified)
```

### Rollback
```
Rollback(tx):
  tx->undo_buffer.ForEachReverse(entry:
    if CatalogUndoEntry:
      catalog_.RollbackEntry(schema, table, tx->tx_id)
    if InsertUndoEntry:
      rg.RevertAppend(tx->tx_id)
    if DeleteUndoEntry:
      for row_id: rg.version_info.ClearDeleteMarker(row_offset)
    if UpdateUndoEntry:
      for row_id: rg.version_info.RevertUpdate(row_offset)
                  // old_values in entry are discarded (not merged)
  )
  tx->local_storage.clear()
  lock(mu_)
  active_transactions_.erase(tx->tx_id)
  GarbageCollect()
  // No WAL entry written: uncommitted changes were never in the WAL
  Time: O(undo_entries + rows_modified)
```

### GarbageCollect
```
GarbageCollect():
  if active_transactions_.empty():
    min_start_time = MAX_TIMESTAMP
  else:
    min_start_time = min(tx.start_time for tx in active_transactions_.values())
  // All UndoBuffer data for transactions whose commit_time < min_start_time
  // is no longer needed by any active reader.
  // In our simple design, UndoBuffers are already freed when the Transaction
  // object is destroyed (after Commit/Rollback deregisters it).
  // GC here marks VersionInfo entries with commit_time < min_start_time as
  // safe to remove from VersionInfo maps (future optimization).
  Time: O(active_transactions_)
```

### MVCC snapshot visibility
```
Transaction T (start_time = S) sees row R:
  Read VersionInfo for R's row_offset (see plan-column-storage VersionInfo::IsVisible)
  T.start_time is compared against marker.commit_time:
    If marker.commit_time < S → change happened before T started → normal visibility rules
    If marker.commit_time >= S or INVALID → change is concurrent or uncommitted
  T sees its own changes via tx_id comparison.
```

## 8. Persistence Model

Transactions themselves are not persisted. Their effects are persisted via:
1. WAL entries (written during Commit, before storage is updated)
2. ColumnSegment blocks (written during Checkpoint)
3. Catalog header block (written during Checkpoint)

On startup, the WAL is replayed to recover any committed transactions not yet in the checkpoint.

## 9. Concurrency Model

| Object | Lock | Usage |
|--------|------|-------|
| `TransactionManager.mu_` | `std::mutex` | Guard `active_transactions_` map during Begin/Commit/Rollback |
| `TransactionManager.wal_write_mu_` | `std::mutex` | Serialize WAL writes across concurrent transactions |
| `commit_counter_` | `std::atomic<timestamp_t>` | Lock-free monotonic counter |
| `next_tx_id_` | `std::atomic<TransactionId>` | Lock-free monotonic counter |

**Write-write conflict**: if T2 tries to update a row that T1 has marked with T1's tx_id (uncommitted), T2 detects the conflict and throws `RuntimeError("write-write conflict")`. T2 is then rolled back.

## 10. Configuration

```cpp
struct TransactionConfig {
    // WAL size threshold to trigger async checkpoint after commit.
    size_t checkpoint_threshold_bytes = 64 * 1024 * 1024; // 64 MiB
};
```

## 11. Testing Strategy

- `TestBeginTransaction`: new transaction has valid tx_id > 0 and start_time
- `TestAutoCommitRollbackOnError`: runtime error during query → auto-commit transaction rolled back
- `TestExplicitBeginCommit`: BEGIN, insert rows, COMMIT → rows visible to next transaction
- `TestExplicitBeginRollback`: BEGIN, insert rows, ROLLBACK → rows gone
- `TestMVCCSnapshotIsolation`: T1 inserts row; T2 started before T1 commits → T2 cannot see row
- `TestMVCCSnapshotAfterCommit`: T1 commits; T3 started after → T3 sees T1's rows
- `TestWriteWriteConflict`: T1 updates row R; T2 also updates R before T1 commits → T2 throws
- `TestReadOnlyTransactionSkipsWAL`: SELECT-only transaction commits without WAL write
- `TestConcurrentTransactions`: T1 and T2 commit in parallel → commit_counter_ increments correctly
- `TestUndoBufferForwardOrder`: verify commit applies entries in forward order
- `TestUndoBufferReverseOrder`: verify rollback applies entries in reverse order
- `TestGCClearsOldVersions`: after all transactions complete, GC reduces active_transactions_ size
- `TestLocalStorageMergedOnCommit`: INSERT rows in local_storage → after commit, visible in scan
- `TestLocalStorageDiscardedOnRollback`: INSERT rows in local_storage → after rollback, gone

## 12. Open Questions

- Concurrent read-write with the single `mu_` lock: for the educational scope this is acceptable. A production implementation would use `std::shared_mutex` to allow concurrent reads.
- Long-running transactions holding back GC: if one transaction stays open for a long time, GC cannot free any MVCC version data. A per-table timeout or warning could be added later.
