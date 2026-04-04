# Transactions, Undo, and MVCC

## Purpose

Provide snapshot isolation for readers/writers, track row/catalog changes in undo entries, and apply or revert those changes on commit/rollback.

## Scope

**In scope:**
- Transaction lifecycle (`BeginTransaction`, `Commit`, `Rollback`).
- Undo buffer structures and forward/reverse iteration.
- Commit-time apply and rollback-time reverse semantics.
- Row-level visibility rules in `VersionInfo`.

**Out of scope:**
- SQL parsing/planning/execution mechanics.
- Physical block persistence details (WAL file format, checkpoint block encoding).

## Primary User Flow

1. Session calls `TransactionManager::BeginTransaction(auto_commit)`.
2. Execution operators append undo entries to `Transaction::undo_buffer`.
3. On commit, manager optionally writes WAL, assigns commit time, and applies undo effects forward.
4. On rollback, manager replays undo entries in reverse and clears staged state.

## System Flow

1. `BeginTransaction` allocates `tx_id` and snapshot `start_time = commit_counter + 1`.
2. `Commit` path (`src/transaction/transaction_manager.cpp`):
- if undo is non-empty and WAL exists, write WAL under `wal_write_mu_`
- assign `commit_time`
- `ApplyUndoBuffer` to commit catalog entries and row version markers
- remove tx from `active_transactions_`
3. `Rollback` path:
- `UndoBufferReverse` restores old row/catalog state
- clears `local_storage`
- removes tx from active map
4. `VersionInfo::IsVisible` decides row visibility per marker type (`INSERT`, `DELETE`, `UPDATE`) and transaction snapshot.

```
BeginTransaction
  -> Transaction{tx_id,start_time,undo_buffer}
  -> operators push UndoEntry variants
Commit:
  -> WriteToWAL (optional)
  -> ApplyUndoBuffer (forward)
Rollback:
  -> UndoBufferReverse
```

## Data Model

- `Transaction` (`src/transaction/transaction.hpp`):
- ids/timestamps: `tx_id`, `start_time`, `commit_time`
- flags: `auto_commit`, `is_invalid`
- `undo_buffer (UndoBuffer)`
- `local_storage (unordered_map<string, DataChunk>)`
- `UndoEntry` variants (`src/transaction/undo_buffer.hpp`):
- `CatalogUndoEntry`
- `InsertUndoEntry`
- `DeleteUndoEntry`
- `UpdateUndoEntry`
- `AlterPartitionUndoEntry`
- `VersionMarker` (`src/storage/column/version_info.hpp`):
- `type`, `tx_id`, `commit_time`
- `VersionInfo` counters:
- `uncommitted_count_`, `delete_update_count_`, `max_insert_commit_time_`

## Interfaces and Contracts

- `std::shared_ptr<Transaction> TransactionManager::BeginTransaction(bool auto_commit=true)`
- contract: returns active transaction with unique `tx_id`.
- `void TransactionManager::Commit(std::shared_ptr<Transaction>)`
- contract: WAL-first when WAL configured; then applies undo entries forward.
- `void TransactionManager::Rollback(std::shared_ptr<Transaction>)`
- contract: restores state by replaying undo entries in reverse order.
- `void Transaction::WriteToWAL(WAL&) const`
- contract: serializes undo entries to WAL in forward order.
- `bool VersionInfo::IsVisible(uint32_t row_offset, const Transaction&) const`
- contract: snapshot visibility check for row markers.

## Dependencies

**Internal modules:**
- `src/catalog` for catalog visibility and DDL commit/rollback hooks.
- `src/storage/column` for row version markers (`VersionInfo`) and row writes.
- `src/storage/wal.*` for commit logging.

**External services/libraries:**
- None.

## Failure Modes and Edge Cases

- Rollback of update uses batched `ColumnChunk::WriteRows`; malformed row/column metadata can skip recovery writes.
- Explicit transaction errors in `Connection::Query` do not auto-rollback transaction scope.
- `TransactionManager::GarbageCollect` is currently a stub; old row versions are not physically GCed.
- Visibility behavior:
- uncommitted inserts by other tx are hidden
- uncommitted deletes by other tx remain visible
- uncommitted updates by other tx are hidden

## Observability and Debugging

- Useful entry points:
- lifecycle: `src/transaction/transaction_manager.cpp`
- WAL serialization: `src/transaction/transaction.cpp:WriteToWAL`
- row visibility decisions: `src/storage/column/version_info.cpp:IsVisible`
- No built-in transaction metrics/logging.

## Risks and Notes

- Commit ordering is WAL-first then in-memory apply, which is good for durability, but failure handling between those phases relies on replay correctness.
- Undo data stores full inserted/updated chunks for WAL/rollback; large transactions can retain substantial in-memory state.

Changes:

