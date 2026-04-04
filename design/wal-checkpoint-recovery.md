# WAL, Checkpointing, and Recovery

## Purpose

Provide durability for committed changes by logging undo-derived redo entries to WAL, checkpointing catalog/segment metadata, and replaying WAL on startup.

## Scope

**In scope:**
- WAL file format, append, flush, replay, and truncation.
- Commit-time WAL emission from transactions.
- Checkpoint sequence and restart recovery flow in `Database`.

**Out of scope:**
- Query execution logic that creates undo entries.
- Block cache internals.

## Primary User Flow

1. Write transaction modifies state and fills undo buffer.
2. `TransactionManager::Commit` writes undo entries to WAL (if WAL enabled), then applies commit markers.
3. `CheckpointManager::CreateCheckpoint` writes checkpoint marker, flushes row groups/catalog metadata, flushes dirty blocks, truncates WAL tail before marker.
4. On restart, `Database::InitPersistent` deserializes catalog block 0 and replays WAL entries after last checkpoint marker.

## System Flow

1. Transaction commit (`src/transaction/transaction_manager.cpp`):
- lock `wal_write_mu_`
- call `Transaction::WriteToWAL` -> `WAL::Write*` entries + `Flush`
2. Checkpoint (`src/checkpoint/checkpoint_manager.cpp`):
- skip if already checkpointing or uncommitted markers exist
- `WriteCheckpointMarker` + `Flush`
- pin/allocate block 0
- `Catalog::FlushAllRowGroups` then `Catalog::Serialize`
- `BufferManager::Flush`
- `WAL::Truncate`
3. Recovery (`src/main/database.cpp`):
- load catalog metadata from block 0 (if present)
- read WAL entries, find last `WAL_CHECKPOINT`, replay later entries
- recreate write-mode WAL file

```
Commit
  -> Transaction::WriteToWAL
  -> WAL::Flush
Checkpoint
  -> WAL checkpoint marker
  -> flush row groups + catalog block0 + dirty blocks
  -> WAL::Truncate
Restart
  -> Catalog::Deserialize(block0)
  -> Replay WAL entries after last checkpoint marker
```

## Data Model

- `WALEntryHeader` (`src/storage/wal.hpp`):
- `type (uint8_t)`
- `data_size (uint32_t)`
- `WALEntry`:
- `type (WALEntryType)`
- `data (vector<uint8_t>)`
- Entry types:
- `WAL_CREATE_TABLE`, `WAL_DROP_TABLE`, `WAL_INSERT`, `WAL_DELETE`, `WAL_UPDATE`, `WAL_CREATE_SCHEMA`, `WAL_DROP_SCHEMA`, `WAL_CHECKPOINT`, `WAL_ALTER_TABLE`
- `WAL` state:
- `write_buffer_`, `file_size_`, `checkpoint_pos_`

## Interfaces and Contracts

- `WAL::Create(path)` / `WAL::OpenForReplay(path)`
- contract: create truncates file; replay opens read-only sequential stream.
- `WAL::WriteInsert/WriteDelete/WriteUpdate/WriteCreateTable/WriteDropTable/WriteAlterTable/...`
- contract: append serialized payload to in-memory write buffer.
- `WAL::Flush()`
- contract: writes full buffer and fsyncs; throws `IOError` on failure.
- `bool WAL::ReadNextEntry(WALEntry&)`
- contract: returns `false` on EOF/truncated tail.
- `WAL::Truncate()`
- contract: keeps bytes from last checkpoint marker onward.
- `bool CheckpointManager::CreateCheckpoint()`
- contract: returns `false` when skipped (already running or uncommitted markers).

## Dependencies

**Internal modules:**
- `src/transaction/transaction.*` for commit-time log production.
- `src/catalog/catalog.*` for checkpoint serialization and replay application helpers.
- `src/main/database.cpp` for replay and lifecycle integration.
- `src/storage/block_file.*`, `src/storage/buffer_manager.*` for durable block writes.

**External services/libraries:**
- POSIX file APIs (`open`, `read`, `write`, `lseek`, `ftruncate`, `fsync`).

## Failure Modes and Edge Cases

- Replay ignores schema-level WAL entries in `Database::ReplayWAL` default branch.
- Checkpoint marker is written/flushed before catalog block is rewritten; crash during checkpoint can lose post-marker state if replay starts after marker.
- `WAL::ReadNextEntry` treats truncated tail entries as EOF (`false`), which avoids crash but may silently skip partial trailing data.
- `WAL::Truncate` path does not check all `ftruncate/lseek` return values.

## Observability and Debugging

- WAL behavior tests: `test/storage/test_wal.cpp`.
- Recovery/startup debug points: `src/main/database.cpp:InitPersistent` and `ReplayWAL`.
- Checkpoint debug point: `src/checkpoint/checkpoint_manager.cpp:CreateCheckpoint`.
- No structured logging around WAL replay decisions.

## Risks and Notes

- `WAL::SerializeChunk`/`DeserializeChunkPayload` compatibility is critical; any mismatch breaks replay correctness.
- Async checkpoint API exists (`ScheduleAsyncCheckpoint`) but lifecycle currently uses synchronous checkpoint during shutdown.

Changes:

