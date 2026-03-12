# Feature: Column Storage

## 1. Purpose

Column Storage is the physical organization of table data on disk and in memory. Tables are partitioned horizontally into `RowGroup`s of a fixed row count (122,880 rows). Within each `RowGroup`, each column has exactly one `ColumnChunk`. A `ColumnChunk` is further divided into `ColumnSegment`s, each of which maps to a single block in the `BufferManager`. This layout enables column-pruning (only requested columns are read), zone-map skipping (entire RowGroups skipped on predicate mismatches), and independent block eviction per column.

## 2. Responsibilities

- `RowGroup`: own the `ColumnChunk` for each column; track the row count in this group; provide `Scan()` and `Append()` operations; apply MVCC row-visibility filtering during scans; maintain zone maps (min/max per ColumnChunk) for scan skipping
- `ColumnChunk`: own one or more `ColumnSegment`s; provide `Scan(row_offset, output_vector)` that decompresses the appropriate segment; provide `Flush(writer)` that compresses buffered values into a new segment block
- `ColumnSegment`: store the `BlockId` for the compressed block, the compression type, the row count, and min/max statistics for this segment; not the data itself (data lives in the `BufferManager` block)
- `TransactionLocalStorage`: hold un-merged INSERT rows for a transaction (private column vectors per table); merged into the main `RowGroup` on commit
- `VersionInfo`: per-RowGroup structure tracking delete markers and update markers for MVCC (used during scan to filter rows)

## 3. Non-Responsibilities

- Does not implement compression algorithms (those are in `plan-column-compression`)
- Does not manage the buffer pool (that is `BufferManager`)
- Does not write the WAL (that is the Transaction commit path)
- Does not implement query operators over column data (those are in `plan-vectorized-operators`)
- Does not manage catalog metadata (table name, schema — that is `plan-catalog`)

## 4. Architecture Design

```
Table (owned by Catalog)
  |
  +-- row_groups_: vector<RowGroup>
        |
        +-- RowGroup 0  (rows 0 to 122,879)
        |     |
        |     +-- column_chunks_[0]: ColumnChunk  (col: order_id, INT64)
        |     |     +-- segments_[0]: ColumnSegment  (block_id=10, BIT_PACKED, 122880 rows)
        |     |
        |     +-- column_chunks_[1]: ColumnChunk  (col: amount, FLOAT64)
        |     |     +-- segments_[0]: ColumnSegment  (block_id=11, UNCOMPRESSED, 122880 rows)
        |     |
        |     +-- version_info_: VersionInfo  (delete/update markers for MVCC)
        |
        +-- RowGroup 1  (rows 122,880 to 245,759)
        |     ...
        |
        +-- TransactionLocalStorage (private to active transactions)
              +-- pending_rows_: DataChunk  (un-committed INSERT rows)

BufferManager
  block 10: compressed INT64 array (bit-packed)
  block 11: raw FLOAT64 array
  ...
```

## 5. Core Data Structures (C++)

```cpp
// src/storage/column/column_segment.hpp
#pragma once
#include "common/types.hpp"

namespace cppcoldb {

enum class CompressionType : uint8_t {
    UNCOMPRESSED = 0,
    RLE          = 1,
    BIT_PACKED   = 2,
    DELTA        = 3,
    DICTIONARY   = 4,
};

// Statistics for one ColumnSegment (used for zone-map skip decisions).
struct SegmentStats {
    Value   min_val;
    Value   max_val;
    bool    has_nulls = false;
};

// Metadata for one compressed block of column data.
struct ColumnSegment {
    BlockId         block_id;        // block in BufferManager holding compressed data
    CompressionType compression;
    TypeId          column_type;
    uint32_t        row_count;       // rows stored in this segment
    uint32_t        row_offset;      // starting row index within the RowGroup
    SegmentStats    stats;
};

} // namespace cppcoldb
```

```cpp
// src/storage/column/column_chunk.hpp
#pragma once
#include <vector>
#include "storage/column/column_segment.hpp"
#include "common/data_chunk.hpp"

namespace cppcoldb {

class BufferManager;

// Scan state for reading from a ColumnChunk.
struct ColumnScanState {
    size_t segment_idx    = 0;   // current segment being read
    size_t row_in_segment = 0;   // next row to read within current segment
};

// One column's worth of data within a RowGroup.
class ColumnChunk {
public:
    ColumnChunk(TypeId type, BufferManager& bm);

    // Read up to count rows starting at scan_state position into output_vector.
    // Returns the number of rows read.
    size_t Scan(ColumnScanState& state, size_t count, DataVector& output);

    // Compress pending_data_ and write it as a new ColumnSegment block.
    // Called during checkpoint or transaction commit merge.
    void Flush();

    // Append rows from a DataVector (used by TransactionLocalStorage merge).
    void AppendFromVector(const DataVector& vec, size_t count);

    TypeId                     column_type() const { return type_; }
    size_t                     RowCount()    const;
    const SegmentStats&        Stats()       const; // combined stats across all segments

private:
    TypeId                     type_;
    BufferManager&             bm_;
    std::vector<ColumnSegment> segments_;
    DataVector                 pending_data_; // buffered rows not yet written to a segment
};

} // namespace cppcoldb
```

```cpp
// src/storage/column/row_group.hpp
#pragma once
#include <vector>
#include <memory>
#include "storage/column/column_chunk.hpp"
#include "storage/column/version_info.hpp"

namespace cppcoldb {

class Transaction;

class RowGroup {
public:
    RowGroup(uint32_t row_group_id, const std::vector<TypeId>& col_types,
             BufferManager& bm);

    // Scan up to STANDARD_VECTOR_SIZE rows into chunk (applying MVCC filter).
    // Returns the number of rows placed into chunk.
    size_t Scan(size_t& row_offset, const std::vector<size_t>& col_ids,
                DataChunk& chunk, const Transaction& tx);

    // Check zone maps: returns true if no row in this RowGroup can satisfy filter.
    bool ZoneMapExcludes(const std::vector<LogicalExpr*>& filters) const;

    // Append rows (called during commit merge).
    void Append(const DataChunk& chunk, const std::vector<size_t>& col_ids);

    // Mark appended rows as committed and visible at commit_time.
    void CommitAppend(timestamp_t commit_time);

    // Revert an uncommitted append (rollback).
    void RevertAppend(TransactionId tx_id);

    // Flush all ColumnChunks to their segment blocks.
    void Flush();

    uint32_t                  RowGroupId()  const { return row_group_id_; }
    size_t                    RowCount()    const { return row_count_; }

private:
    uint32_t                        row_group_id_;
    size_t                          row_count_ = 0;
    std::vector<TypeId>             col_types_;
    std::vector<ColumnChunk>        column_chunks_;  // one per column
    std::unique_ptr<VersionInfo>    version_info_;   // MVCC markers
    BufferManager&                  bm_;
};

} // namespace cppcoldb
```

```cpp
// src/storage/column/version_info.hpp
#pragma once
#include <unordered_map>
#include <vector>
#include "common/types.hpp"

namespace cppcoldb {

enum class VersionMarkerType : uint8_t { DELETE, INSERT, UPDATE };

struct VersionMarker {
    VersionMarkerType type;
    TransactionId     tx_id;        // which transaction made this change
    timestamp_t       commit_time;  // INVALID_TIMESTAMP if uncommitted
    // For UPDATE: old values are stored in the UndoBuffer, not here.
};

// Per-RowGroup MVCC version markers.
// row_offset -> most recent VersionMarker (uncommitted or latest committed).
class VersionInfo {
public:
    void MarkDeleted(uint32_t row_offset, TransactionId tx_id);
    void CommitDelete(uint32_t row_offset, timestamp_t commit_time);
    void ClearDeleteMarker(uint32_t row_offset); // rollback

    void MarkUpdated(uint32_t row_offset, TransactionId tx_id);
    void CommitUpdate(uint32_t row_offset, timestamp_t commit_time);
    void RevertUpdate(uint32_t row_offset);

    // Returns true if this row should be included for transaction tx.
    bool IsVisible(uint32_t row_offset, const Transaction& tx) const;

    bool HasAnyMarkers() const { return !markers_.empty(); }

private:
    std::unordered_map<uint32_t, VersionMarker> markers_;
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
namespace cppcoldb {

// RowGroup
class RowGroup {
public:
    size_t   Scan(size_t& row_offset, const std::vector<size_t>& col_ids,
                  DataChunk& chunk, const Transaction& tx);
    bool     ZoneMapExcludes(const std::vector<LogicalExpr*>& filters) const;
    void     Append(const DataChunk& chunk, const std::vector<size_t>& col_ids);
    void     CommitAppend(timestamp_t commit_time);
    void     RevertAppend(TransactionId tx_id);
    void     Flush();
    size_t   RowCount() const;
};

// ColumnChunk
class ColumnChunk {
public:
    size_t  Scan(ColumnScanState& state, size_t count, DataVector& output);
    void    Flush();
    void    AppendFromVector(const DataVector& vec, size_t count);
    size_t  RowCount() const;
};

// VersionInfo
class VersionInfo {
public:
    void   MarkDeleted(uint32_t row_offset, TransactionId tx_id);
    void   CommitDelete(uint32_t row_offset, timestamp_t commit_time);
    void   ClearDeleteMarker(uint32_t row_offset);
    bool   IsVisible(uint32_t row_offset, const Transaction& tx) const;
};

} // namespace cppcoldb
```

## 7. Internal Algorithms

### RowGroup::Scan
```
Scan(row_offset, col_ids, chunk, tx):
  rows_to_read = min(STANDARD_VECTOR_SIZE, row_count_ - row_offset)
  if rows_to_read == 0: return 0

  // Read requested columns
  for c in col_ids:
    scan_state = ColumnScanState{segment for row_offset, offset within segment}
    column_chunks_[c].Scan(scan_state, rows_to_read, chunk.columns[c_out_idx])

  chunk.count = rows_to_read

  // Apply MVCC filter: compact out invisible rows
  if version_info_.HasAnyMarkers():
    selection = []
    for i in 0..rows_to_read:
      if version_info_.IsVisible(row_offset + i, tx):
        selection.append(i)
    DataChunkCompact(chunk, selection, chunk)  // in-place compact

  row_offset += rows_to_read
  return chunk.count
  Time: O(rows_to_read * col_ids.size())
```

### ColumnChunk::Scan
```
Scan(scan_state, count, output_vec):
  rows_read = 0
  while rows_read < count AND scan_state.segment_idx < segments_.size():
    seg = segments_[scan_state.segment_idx]
    handle = bm_.Pin(seg.block_id)
    decompressed = Decompress(handle.Data(), seg.compression, seg.row_count, type_)
    available = seg.row_count - scan_state.row_in_segment
    to_copy = min(count - rows_read, available)
    CopyRows(decompressed, scan_state.row_in_segment, to_copy, output_vec, rows_read)
    rows_read += to_copy
    scan_state.row_in_segment += to_copy
    if scan_state.row_in_segment >= seg.row_count:
      scan_state.segment_idx++
      scan_state.row_in_segment = 0
    // handle goes out of scope here → Unpin
  return rows_read
  Time: O(to_copy * decompression_cost)
```

### RowGroup::Append (commit merge)
```
Append(chunk, col_ids):
  for i, c in enumerate(col_ids):
    column_chunks_[c].AppendFromVector(chunk.columns[i], chunk.count)
  row_count_ += chunk.count
```

### VersionInfo::IsVisible
```
IsVisible(row_offset, tx):
  if not markers_.contains(row_offset): return true  // unmodified row

  marker = markers_[row_offset]
  if marker.type == DELETE:
    if marker.tx_id == tx.tx_id: return false         // own delete
    if marker.commit_time == INVALID_TIMESTAMP: return true  // uncommitted by other
    if marker.commit_time < tx.start_time: return false      // deleted before T started
    return true                                              // deleted after T started

  if marker.type == INSERT:
    if marker.tx_id == tx.tx_id: return true          // own insert
    if marker.commit_time == INVALID_TIMESTAMP: return false // uncommitted by other
    if marker.commit_time < tx.start_time: return true       // committed before T
    return false

  return true  // UPDATE: row itself is visible; value resolution handled by UndoBuffer
  Time: O(1)
```

## 8. Persistence Model

Column data is persisted as compressed blocks in the `BlockFile`. Each `ColumnSegment` stores a `BlockId` that refers to a block in the file. During checkpoint, all dirty column segment blocks are flushed via `BufferManager::Flush()`.

```
RowGroup metadata serialized to checkpoint file header:
  row_group_id     : uint32
  row_count        : uint64
  num_columns      : uint32
  for each ColumnChunk:
    num_segments   : uint32
    for each ColumnSegment:
      block_id     : uint64
      compression  : uint8
      row_count    : uint32
      row_offset   : uint32
      stats.min    : Value (serialized)
      stats.max    : Value (serialized)
```

## 9. Concurrency Model

| Object | Lock | Usage |
|--------|------|-------|
| `RowGroup` | None (MVCC for reads) | Single writer per transaction (via TransactionLocalStorage merge at commit); concurrent reads use MVCC |
| `ColumnChunk` | None | Written by committing transaction; read by scan operators |
| `VersionInfo` | None (single writer: the committing transaction) | Read during scan by any transaction |

**MVCC read safety**: readers traverse `VersionInfo` markers using their snapshot timestamp without locking. Writers append markers only from the committing transaction thread. For the single-node, single-writer educational scope this is safe.

## 10. Configuration

```cpp
static constexpr uint32_t ROW_GROUP_SIZE = 122880; // 120 * 1024 rows per RowGroup
// block_size: controlled by BufferManager (default 256 KiB)
```

## 11. Testing Strategy

- `TestColumnChunkScanUncompressed`: write 1000 rows, scan → all rows returned correctly
- `TestColumnChunkScanMultipleSegments`: rows span 2 segments → scan reads across segment boundary
- `TestColumnChunkFlush`: append rows, flush → new ColumnSegment with block_id in BufferManager
- `TestRowGroupAppendScan`: append 5000 rows, scan all → correct values in column order
- `TestRowGroupZoneMapSkip`: push filter that excludes all values in RG → ZoneMapExcludes returns true
- `TestRowGroupZoneMapNoSkip`: filter that might match → ZoneMapExcludes returns false
- `TestRowGroupScanMVCCDeleteVisible`: row deleted by another committed txn before T starts → excluded
- `TestRowGroupScanMVCCDeleteInvisible`: row deleted by uncommitted txn → still visible to T
- `TestRowGroupCommitAppend`: append rows under T, commit → rows visible to later transaction
- `TestRowGroupRevertAppend`: append rows under T, rollback → rows gone
- `TestVersionInfoMarkDelete`: mark row deleted, check IsVisible for owner txn → false
- `TestVersionInfoCommitDelete`: commit delete, check IsVisible for txn with later start_time → false
- `TestColumnScanNullHandling`: rows with NULL values → validity bit correctly set in DataVector
- `TestRowGroupFlush`: flush all column chunks → ColumnSegments have valid block_ids

## 12. Open Questions

- ColumnChunk with multiple segments per chunk: the initial implementation will use one segment per ColumnChunk (flushed as a single block). Multi-segment support is a straightforward extension.
- Large rows exceeding block size: rows whose single column value exceeds `block_size` are not handled; VARCHAR fields are assumed to be reasonably sized.
