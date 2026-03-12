# Feature: Write-Ahead Log (WAL)

## 1. Purpose

The Write-Ahead Log (WAL) ensures durability: every committed change is written and fsynced to the WAL file before it is applied to the main storage. On a crash, the WAL can be replayed at startup to recover any committed transactions that were not yet checkpointed. In-memory databases skip the WAL entirely. The WAL uses a simple sequential binary format: each entry is a fixed-size header (`[type: uint8][size: uint32]`) followed by a variable-length data payload.

## 2. Responsibilities

- `WAL::WriteInsert(table_id, chunk)`: serialize an INSERT DataChunk to the WAL buffer
- `WAL::WriteDelete(table_id, row_ids)`: serialize deleted row IDs to the WAL buffer
- `WAL::WriteUpdate(table_id, col_ids, row_ids, new_values)`: serialize UPDATE changes
- `WAL::WriteCatalogEntry(type, entry)`: serialize CREATE TABLE / DROP TABLE / CREATE SCHEMA / DROP SCHEMA
- `WAL::WriteCheckpointMarker()`: write a WAL_CHECKPOINT sentinel entry marking the end of a checkpoint
- `WAL::Flush()`: write the in-memory WAL buffer to the file and call `fsync()`
- `WAL::OpenForReplay()`: open an existing WAL file in read mode
- `WAL::ReadEntry(header, data)`: read the next WAL entry from the file; return false at EOF or truncated entry
- `WAL::Truncate()`: remove all entries before the last checkpoint marker (or truncate entirely)
- `WAL::Close()` / `WAL::Create(path)`: lifecycle management

## 3. Non-Responsibilities

- Does not apply WAL entries to storage (the replay logic in `StorageManager` does that)
- Does not manage the Catalog (that is the Catalog's responsibility)
- Does not decide when to trigger a checkpoint (that is `CheckpointManager`)
- Does not buffer changes before commit (that is the `UndoBuffer` in the transaction)
- Does not provide random access to WAL entries

## 4. Architecture Design

```
Transaction commit path:
  TransactionManager::Commit(tx)
        |
        v
  Transaction::WriteToWAL(wal)
    for each UndoBuffer entry in commit order:
      wal.WriteInsert() / WriteDelete() / WriteUpdate() / WriteCatalogEntry()
        |
        v
  WAL buffer (std::vector<uint8_t>, in-memory)
        |
        v
  WAL::Flush()
    write buffer to wal_file_
    fsync(wal_fd_)          ← durability guarantee
        |
        v
  Commit proceeds: apply UndoBuffer to storage, assign commit_time

WAL file format:
  [entry 1 header][entry 1 data][entry 2 header][entry 2 data]...
  Entry header: [type: uint8][data_size: uint32] = 5 bytes
  Entry data:   variable-length serialized payload
```

**WAL entry types:**
```
WAL_CREATE_TABLE  = 1
WAL_DROP_TABLE    = 2
WAL_INSERT        = 3
WAL_DELETE        = 4
WAL_UPDATE        = 5
WAL_CREATE_SCHEMA = 6
WAL_DROP_SCHEMA   = 7
WAL_CHECKPOINT    = 8
```

## 5. Core Data Structures (C++)

```cpp
// src/storage/wal.hpp
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "common/types.hpp"
#include "common/data_chunk.hpp"

namespace cppcoldb {

enum class WALEntryType : uint8_t {
    WAL_CREATE_TABLE  = 1,
    WAL_DROP_TABLE    = 2,
    WAL_INSERT        = 3,
    WAL_DELETE        = 4,
    WAL_UPDATE        = 5,
    WAL_CREATE_SCHEMA = 6,
    WAL_DROP_SCHEMA   = 7,
    WAL_CHECKPOINT    = 8,
};

// Fixed 5-byte entry header.
struct WALEntryHeader {
    uint8_t  type;       // WALEntryType
    uint32_t data_size;  // bytes of payload following this header
};
static_assert(sizeof(WALEntryHeader) == 5, "WALEntryHeader size changed");

// One decoded WAL entry (used during replay).
struct WALEntry {
    WALEntryType         type;
    std::vector<uint8_t> data; // raw payload bytes
};

class WAL {
public:
    // Create a new WAL file at path (truncates if exists).
    static std::unique_ptr<WAL> Create(const std::string& path);

    // Open an existing WAL file for replay.
    static std::unique_ptr<WAL> OpenForReplay(const std::string& path);

    ~WAL();

    // Write operations (append to in-memory buffer).
    void WriteInsert(const std::string& schema, const std::string& table,
                     const DataChunk& chunk);
    void WriteDelete(const std::string& schema, const std::string& table,
                     const std::vector<row_t>& row_ids);
    void WriteUpdate(const std::string& schema, const std::string& table,
                     const std::vector<size_t>& col_ids,
                     const std::vector<row_t>& row_ids,
                     const DataChunk& new_values);
    void WriteCreateTable(const std::string& schema, const std::string& table,
                          const std::vector<std::string>& col_names,
                          const std::vector<TypeId>& col_types);
    void WriteDropTable(const std::string& schema, const std::string& table);
    void WriteCreateSchema(const std::string& schema);
    void WriteDropSchema(const std::string& schema);
    void WriteCheckpointMarker();

    // Flush the in-memory buffer to disk and fsync.
    // Throws IOError if the write or fsync fails.
    void Flush();

    // Replay: read the next entry from the WAL file.
    // Returns false when EOF is reached or a truncated entry is detected.
    bool ReadNextEntry(WALEntry& out);

    // Remove WAL entries that precede the last checkpoint marker.
    void Truncate();

    // Close the WAL file handle.
    void Close();

    bool IsEmpty() const { return file_size_ == 0; }
    size_t FileSizeBytes() const { return file_size_; }

private:
    explicit WAL(int fd, bool read_only);

    // Serialize a typed header + data payload into write_buffer_.
    void AppendEntry(WALEntryType type, const uint8_t* data, size_t size);

    // Binary serialization helpers.
    static void SerializeChunk(const DataChunk& chunk, std::vector<uint8_t>& buf);
    static void SerializeString(const std::string& s, std::vector<uint8_t>& buf);
    static void SerializeRowIds(const std::vector<row_t>& ids, std::vector<uint8_t>& buf);

    int                  fd_;           // file descriptor
    bool                 read_only_;
    std::vector<uint8_t> write_buffer_; // pending data not yet flushed
    size_t               file_size_;    // bytes written to disk so far
    size_t               checkpoint_pos_ = 0; // position of last WAL_CHECKPOINT entry
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

class WAL {
public:
    static std::unique_ptr<WAL> Create(const std::string& path);
    static std::unique_ptr<WAL> OpenForReplay(const std::string& path);

    void WriteInsert(const std::string& schema, const std::string& table,
                     const DataChunk& chunk);
    void WriteDelete(const std::string& schema, const std::string& table,
                     const std::vector<row_t>& row_ids);
    void WriteUpdate(const std::string& schema, const std::string& table,
                     const std::vector<size_t>& col_ids,
                     const std::vector<row_t>& row_ids,
                     const DataChunk& new_values);
    void WriteCreateTable(const std::string& schema, const std::string& table,
                          const std::vector<std::string>& col_names,
                          const std::vector<TypeId>& col_types);
    void WriteDropTable(const std::string& schema, const std::string& table);
    void WriteCheckpointMarker();

    void Flush();
    bool ReadNextEntry(WALEntry& out);
    void Truncate();
    void Close();

    bool   IsEmpty() const;
    size_t FileSizeBytes() const;
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### WAL write path (AppendEntry)
```
AppendEntry(type, data, size):
  header = WALEntryHeader{type, size}
  Append &header (5 bytes) to write_buffer_
  Append data    (size bytes) to write_buffer_
  // No file I/O here; all in memory until Flush()
  Time: O(size)

Flush():
  if write_buffer_.empty(): return
  n = write(fd_, write_buffer_.data(), write_buffer_.size())
  if n != write_buffer_.size():
    throw IOError("WAL write failed: partial write")
  fsync(fd_)    // guarantee durability
  file_size_ += write_buffer_.size()
  write_buffer_.clear()
  Time: O(buffer_size) + fsync latency
```

### WAL serialization format for INSERT
```
WriteInsert(schema, table, chunk):
  buf = []
  SerializeString(schema, buf)          // uint16 length + bytes
  SerializeString(table, buf)
  write uint32: chunk.count
  write uint32: chunk.ColumnCount()
  for each DataVector col in chunk.columns:
    write uint8: col.type
    write validity bitmap (ceil(count/8) bytes)
    for each non-null value in col: write typed value
  AppendEntry(WAL_INSERT, buf.data(), buf.size())
  Time: O(rows * cols)
```

### WAL replay (ReadNextEntry)
```
ReadNextEntry(out):
  n = read(fd_, &header, 5)
  if n == 0: return false  // EOF
  if n < 5:
    // Truncated header: crash mid-write
    return false

  if header.data_size > MAX_WAL_ENTRY_SIZE:
    throw IOError("WAL entry data_size too large: file corrupted")

  out.data.resize(header.data_size)
  n = read(fd_, out.data.data(), header.data_size)
  if n < header.data_size:
    // Truncated entry at end of file: crash mid-write, safe to stop
    return false

  out.type = static_cast<WALEntryType>(header.type)
  return true
  Time: O(header.data_size)
```

### WAL truncation
```
Truncate():
  // Remove entries before the last WAL_CHECKPOINT marker.
  // Strategy: rewrite everything after checkpoint_pos_ to a temp file,
  //           then rename over the original.
  if checkpoint_pos_ == 0: truncate file to 0 (remove entirely)
  else:
    read bytes [checkpoint_pos_, file_size_] from fd_
    write them to a temp file
    rename temp to wal_path_
    reopen fd_
    file_size_ = new_size
    checkpoint_pos_ = 0
  Time: O(bytes_after_checkpoint)
```

## 8. Persistence Model

```
WAL file: <db_path>.wal
  Sequential binary file, append-only during normal operation.
  Format: stream of WALEntry records:
    [WALEntryHeader: 5 bytes][payload: data_size bytes]
    ...

WAL_INSERT payload:
  schema_len: uint16, schema: bytes
  table_len:  uint16, table: bytes
  row_count:  uint32
  col_count:  uint32
  for each column:
    type:     uint8
    validity: ceil(row_count/8) bytes
    values:   typed binary data (depends on TypeId)

WAL_DELETE payload:
  schema_len, schema, table_len, table
  row_count: uint32
  row_ids:   row_t[row_count]

WAL_CREATE_TABLE payload:
  schema_len, schema, table_len, table
  col_count: uint32
  for each column:
    name_len: uint16, name: bytes, type: uint8

WAL_CHECKPOINT payload: empty (data_size = 0)
```

## 9. Concurrency Model

| Object | Lock | Usage |
|--------|------|-------|
| `WAL` (write path) | `WAL write lock` (held by TransactionManager::Commit) | Only one transaction writes WAL at a time |
| `WAL` (replay path) | Single-threaded startup | No concurrent access during replay |

The WAL write lock is a `std::mutex` held by `TransactionManager` for the duration of WAL write + fsync. Multiple concurrent transactions serialize on this lock; they do not hold it during storage application.

## 10. Configuration

```cpp
struct WALConfig {
    // Maximum WAL file size before a checkpoint is triggered (bytes).
    size_t checkpoint_threshold_bytes = 64 * 1024 * 1024; // 64 MiB

    // Maximum size of a single WAL entry payload (safety limit).
    size_t max_entry_size_bytes = 128 * 1024 * 1024; // 128 MiB
};
```

## 11. Testing Strategy

- `TestWALCreateFlushRead`: create WAL, write an INSERT entry, flush, replay → entry read back correctly
- `TestWALEntryHeaderFormat`: verify WALEntryHeader is 5 bytes with correct field layout
- `TestWALInsertRoundTrip`: write INSERT chunk, flush, replay → DataChunk reconstructed correctly
- `TestWALDeleteRoundTrip`: write DELETE row_ids, flush, replay → row IDs match
- `TestWALCreateTableRoundTrip`: write CREATE TABLE entry → replayed correctly
- `TestWALCheckpointMarker`: write entries, checkpoint marker, more entries → marker separates groups
- `TestWALTruncate`: write, checkpoint, more entries, truncate → only post-checkpoint entries remain
- `TestWALPartialEntryAtEOF`: truncate WAL file mid-entry → ReadNextEntry returns false safely
- `TestWALFsyncCalled`: mock file write; verify fsync called after Flush()
- `TestWALInMemorySkipped`: in-memory database → WriteInsert is no-op (not called by commit path)
- `TestWALMultipleFlushes`: append to WAL in multiple Flush() calls → all entries readable in order
- `TestWALFileSizeTracked`: after writes and flush, FileSizeBytes() matches actual file size

## 12. Open Questions

None.
