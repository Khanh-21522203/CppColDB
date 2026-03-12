# Feature: Checkpoint

## 1. Purpose

The `CheckpointManager` periodically writes the full current database state (catalog snapshot + all column data) to the persistent `BlockFile`, then truncates the WAL to remove entries that are now safely on disk. After a checkpoint, crash recovery only needs to replay WAL entries written after the checkpoint marker. Checkpoints are triggered automatically when the WAL grows past a size threshold (submitted as a background task) or explicitly by the `CHECKPOINT` statement. In-memory databases never checkpoint.

## 2. Responsibilities

- `CheckpointManager::CreateCheckpoint()`: acquire the checkpoint lock; write a `WAL_CHECKPOINT` marker; flush and seal the current WAL; serialize the Catalog to the file header block; flush all dirty `ColumnChunk` segment blocks via `BufferManager`; truncate the WAL
- Acquire a `checkpoint_lock_` (non-blocking try-lock) to prevent concurrent checkpoints
- Pause WAL writes during the checkpoint window by holding the WAL write lock while writing the checkpoint marker and sealing the WAL file
- After flushing all column data, call `BlockFile::Sync()` to ensure all blocks are durable
- Release locks and signal completion
- `AsyncCheckpointTask`: a background `Task` submitted to the `TaskScheduler`; wraps `CreateCheckpoint()` so it runs off the commit hot path

## 3. Non-Responsibilities

- Does not flush individual transactions to WAL (that is `Transaction::WriteToWAL()`)
- Does not manage the buffer pool eviction policy (that is `BufferManager`)
- Does not decide when a checkpoint should fire based on time intervals (only WAL size triggers and explicit calls are in scope)
- Does not handle distributed (multi-node) checkpoints
- Does not compress the catalog snapshot beyond what the Catalog serializer produces

## 4. Architecture Design

```
Transaction commit
  → WAL grows past threshold
  → TransactionManager submits AsyncCheckpointTask to TaskScheduler
        |
        v
Worker thread picks up AsyncCheckpointTask
        |
        v
CheckpointManager::CreateCheckpoint()
  |
  1. Try-acquire checkpoint_lock_
  2. Check WAL size still above threshold
  3. Acquire WAL write lock
  4. WAL::WriteCheckpointMarker() + WAL::Flush()   ← seal current WAL
  5. Release WAL write lock
  6. Catalog::Serialize(writer)                    ← write schema metadata
  7. For each Table → RowGroup → ColumnChunk:
       ColumnChunk::Flush()                        ← compress + write block
  8. BufferManager::Flush()                        ← write dirty blocks, fsync
  9. WAL::Truncate()                               ← remove pre-checkpoint entries
 10. Release checkpoint_lock_
```

**Checkpoint WAL race**: any WAL writes that happen between step 4 (seal) and step 9 (truncate) go into a new WAL segment. At the end of CreateCheckpoint, if the new segment is non-empty, it becomes the active WAL file; otherwise the WAL file is deleted.

## 5. Core Data Structures (C++)

```cpp
// src/storage/checkpoint_manager.hpp
#pragma once
#include <mutex>
#include <memory>
#include "common/types.hpp"

namespace cppcoldb {

class Catalog;
class BufferManager;
class WAL;
class StorageManager;
class TaskScheduler;

class CheckpointManager {
public:
    CheckpointManager(Catalog& catalog, BufferManager& bm,
                      WAL& wal, StorageManager& storage);

    // Run a full checkpoint synchronously.
    // Returns true if checkpoint ran; false if skipped (lock not acquired
    // or WAL size below threshold).
    bool CreateCheckpoint();

    // Submit an asynchronous checkpoint task to the scheduler.
    void ScheduleAsyncCheckpoint(TaskScheduler& scheduler);

    bool IsCheckpointing() const;

private:
    Catalog&        catalog_;
    BufferManager&  bm_;
    WAL&            wal_;
    StorageManager& storage_;

    std::mutex      checkpoint_lock_;
    bool            is_checkpointing_ = false;
};

} // namespace cppcoldb
```

```cpp
// src/storage/checkpoint_manager.hpp (continued)
// The catalog serialization writer used during checkpoint.
struct CheckpointWriter {
    uint8_t* header_block;  // pointer to the file header block (pinned from BufferManager)
    size_t   offset = 0;    // current write position within header block

    void WriteUInt32(uint32_t v);
    void WriteUInt64(uint64_t v);
    void WriteString(const std::string& s);
    void WriteTypeId(TypeId t);
};

// Background task wrapping CreateCheckpoint.
// src/parallel/task.hpp
struct Task {
    enum class TaskResult { FINISHED, ERROR };
    virtual TaskResult Execute() = 0;
    virtual ~Task() = default;
};

struct AsyncCheckpointTask : Task {
    CheckpointManager& ckpt_mgr;
    TaskResult Execute() override {
        ckpt_mgr.CreateCheckpoint();
        return TaskResult::FINISHED;
    }
};
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class CheckpointManager {
public:
    // Synchronous checkpoint; returns false if skipped.
    bool CreateCheckpoint();

    // Submit async checkpoint to the task scheduler.
    void ScheduleAsyncCheckpoint(TaskScheduler& scheduler);

    bool IsCheckpointing() const;
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### CreateCheckpoint
```
CreateCheckpoint():
  1. if not checkpoint_lock_.try_lock(): return false  // another checkpoint running
  2. is_checkpointing_ = true
  defer: is_checkpointing_ = false; checkpoint_lock_.unlock()

  3. if wal_.FileSizeBytes() < CHECKPOINT_THRESHOLD: return false  // nothing to do

  4. Acquire WAL write lock (mu_wal_write in TransactionManager)
     wal_.WriteCheckpointMarker()
     wal_.Flush()                  // seal with checkpoint marker durably on disk
     Release WAL write lock

  5. header_block = bm_.Pin(CATALOG_HEADER_BLOCK_ID)
     writer = CheckpointWriter{header_block.Data()}
     catalog_.Serialize(writer)    // write all schema and table metadata
     bm_.MarkDirty(CATALOG_HEADER_BLOCK_ID)

  6. for each schema in catalog_:
       for each table in schema:
         for each row_group in table.row_groups:
           for each column_chunk in row_group.column_chunks:
             column_chunk.Flush()    // compress + mark dirty in BufferManager

  7. bm_.Flush()                  // write all dirty column blocks + fsync

  8. wal_.Truncate()              // remove entries before checkpoint marker

  return true
  Time: O(total_dirty_column_bytes) + fsync latency × 2
```

### Catalog::Serialize (format written to header block)
```
Serialize(writer):
  writer.WriteUInt32(schema_count)
  for each schema:
    writer.WriteString(schema.name)
    writer.WriteUInt32(table_count)
    for each table:
      writer.WriteString(table.name)
      writer.WriteUInt32(col_count)
      for each column:
        writer.WriteString(col.name)
        writer.WriteTypeId(col.type)
        writer.WriteBool(col.not_null)
      writer.WriteUInt32(row_group_count)
      for each row_group:
        writer.WriteUInt64(row_group.row_count)
        for each column_chunk:
          writer.WriteUInt32(segment_count)
          for each segment:
            writer.WriteUInt64(segment.block_id)
            writer.WriteUInt8(segment.compression)
            writer.WriteUInt32(segment.row_count)
  Time: O(tables * columns * segments)
```

### Database load: Catalog deserialization (at startup)
```
Deserialize(reader, bm):
  schema_count = reader.ReadUInt32()
  for i in 0..schema_count:
    schema_name = reader.ReadString()
    table_count = reader.ReadUInt32()
    for j in 0..table_count:
      table_name = reader.ReadString()
      col_count = reader.ReadUInt32()
      col_defs = []
      for k in 0..col_count:
        name = reader.ReadString(); type = reader.ReadTypeId()
        not_null = reader.ReadBool()
        col_defs.append({name, type, not_null})
      row_group_count = reader.ReadUInt32()
      row_groups = []
      for r in 0..row_group_count:
        rg = RowGroup(col_types)
        for c in 0..col_count:
          seg_count = reader.ReadUInt32()
          for s in 0..seg_count:
            block_id = reader.ReadUInt64()
            compression = reader.ReadUInt8()
            row_count = reader.ReadUInt32()
            rg.column_chunks[c].AddSegment(block_id, compression, row_count)
        row_groups.append(rg)
      catalog.CreateTable(schema_name, table_name, col_defs, row_groups)
  Time: O(tables * columns * segments)
```

## 8. Persistence Model

The checkpoint writes to two areas of the persistent `BlockFile`:

```
BlockFile layout:
  Block 0: Catalog header block (schema + table metadata + RowGroup/ColumnSegment pointers)
  Block 1..N: Column data blocks (one per ColumnSegment)

Catalog header block format:
  See Catalog::Serialize() above.
  Fixed block size (256 KiB); for large catalogs, a chain of header blocks can be used.

After checkpoint:
  WAL file is truncated (entries before the checkpoint marker are removed).
  The database is self-consistent from Block 0 alone (no WAL needed for startup).
```

## 9. Concurrency Model

| Object | Lock | Usage |
|--------|------|-------|
| `checkpoint_lock_` | `std::mutex` (try_lock) | Prevent concurrent checkpoints |
| WAL write lock | Held briefly in step 4 | Pause in-flight commits while sealing WAL |
| `BufferManager.mu_` | Acquired per-block by Flush() | Protect pool access during write-back |

**Deadlock avoidance**: `checkpoint_lock_` is never held while acquiring `WAL write lock`; the WAL lock is held only for the brief marker-write window, then released before the long column-data flush.

## 10. Configuration

```cpp
struct CheckpointConfig {
    // WAL file size that triggers an automatic async checkpoint.
    size_t checkpoint_threshold_bytes = 64 * 1024 * 1024; // 64 MiB

    // Block ID reserved for the catalog header within the BlockFile.
    BlockId catalog_header_block_id = 0;
};
```

## 11. Testing Strategy

- `TestCheckpointCreatesHeaderBlock`: after checkpoint, Block 0 of BlockFile contains serialized catalog
- `TestCheckpointColumnBlocksWritten`: all ColumnSegment block_ids present in BlockFile after checkpoint
- `TestCheckpointTruncatesWAL`: WAL size decreases after successful checkpoint
- `TestCheckpointMarkerInWAL`: WAL_CHECKPOINT entry written and readable before truncation
- `TestCheckpointSkipsIfAlreadyRunning`: concurrent CreateCheckpoint() → second returns false
- `TestCheckpointSkipsIfBelowThreshold`: WAL under threshold → CreateCheckpoint() returns false
- `TestCheckpointRoundTrip`: checkpoint → reopen database → catalog and data match pre-checkpoint state
- `TestCheckpointWALReplayAfter`: write after checkpoint → WAL contains only post-checkpoint entries
- `TestCheckpointInMemorySkipped`: in-memory database → CreateCheckpoint() is a no-op
- `TestAsyncCheckpointTaskScheduled`: post-commit task submitted to TaskScheduler when WAL above threshold

## 12. Open Questions

- Incremental checkpoints: the current design rewrites all column data every time. A future optimization could track which ColumnChunks are dirty since the last checkpoint and only write those.
- Catalog header block overflow: if the catalog is very large (thousands of tables), 256 KiB may not be enough. A chained header block approach can be added when needed.
