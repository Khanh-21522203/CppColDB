# Columnar Storage and Compression

## Purpose

Store table data in columnar row groups (`RowGroup` -> `ColumnChunk` -> compressed `ColumnSegment` blocks), with MVCC-aware scans, late materialization support, and per-segment zone-map stats.

## Scope

**In scope:**
- Row group append/scan/flush operations.
- Column chunk pending buffer, segment reads/writes, zone-map skip.
- Compression selection and codec encode/decode.
- Segment statistics used by scan pruning.

**Out of scope:**
- Query planning and operator orchestration.
- WAL/checkpoint orchestration.

## Primary User Flow

1. Insert/update operators append values into row-group column chunks.
2. Pending vectors in `ColumnChunk` flush into compressed segment blocks.
3. Scans call `RowGroup::Scan` (or `ScanBatchWithOffsets` + `ScanLate`) and apply MVCC visibility.
4. Segment stats allow `ZoneMapSkipRows` to skip irrelevant ranges.

## System Flow

1. `RowGroup::Append` appends per-column vectors and marks inserted row markers when transaction id is present.
2. `ColumnChunk::AppendFromVector` fills pending vector and flushes when capacity reaches `STANDARD_VECTOR_SIZE`.
3. `ColumnChunk::Flush` chooses codec with `SelectCompression`, allocates block from buffer manager, writes compressed payload + metadata.
4. `RowGroup::Scan` reads requested columns via `ColumnChunk::Scan`, then applies `VersionInfo::IsVisible` filtering.
5. Late materialization path:
- `ScanBatchWithOffsets` reads filter columns + offsets
- predicate selects offsets
- `ScanLate` fetches payload columns only for selected rows
6. In-place updates use `ColumnChunk::WriteRow` / `WriteRows` (segment decompress-modify-recompress).

```
Append -> pending DataVector
  -> (threshold) Flush
     -> Compress + allocate block + add ColumnSegment
Scan
  -> read segments/pending
  -> MVCC visibility filter
  -> optional late payload fetch by offsets
```

## Data Model

- `RowGroup` (`src/storage/column/row_group.hpp`):
- `row_group_id_`, `row_count_`, `column_chunks_`, `version_info_`
- pending append state: `pending_append_start_`, `pending_append_tx_id_`
- `ColumnChunk` (`src/storage/column/column_chunk.hpp`):
- `type_`, `segments_`, `pending_data_`, `pending_row_offset_`, `combined_stats_`
- `ColumnSegment` (`src/storage/column/column_segment.hpp`):
- `block_id`, `compression`, `column_type`, `row_count`, `row_offset`, `stats`
- `SegmentStats`: `min_val`, `max_val`, `has_nulls`
- Compression header (`src/storage/column/compression.hpp`):
- `SegmentHeader{compression_type,row_count,null_bitmap_size}`
- Codecs:
- `UNCOMPRESSED`, `RLE`, `BIT_PACKED`, `DELTA`, `DICTIONARY`

## Interfaces and Contracts

- `size_t RowGroup::Scan(size_t& row_offset, const vector<size_t>& col_ids, DataChunk&, const Transaction&)`
- contract: returns up to `STANDARD_VECTOR_SIZE` visible rows.
- `size_t RowGroup::ScanBatchWithOffsets(...)` / `void RowGroup::ScanLate(...)`
- contract: `ScanLate` expects sorted offsets already filtered by visibility.
- `void ColumnChunk::Flush()`
- contract: no-op when pending buffer is empty.
- `void ColumnChunk::WriteRows(const vector<uint32_t>& row_offsets, const DataVector& src)`
- contract: `row_offsets` must be non-decreasing and `src.count` must match.
- `CompressionChoice SelectCompression(const DataVector&)`
- contract: chooses codec based on simple heuristics per vector.

## Dependencies

**Internal modules:**
- `src/storage/buffer_manager.*` for block pin/allocate/flush.
- `src/storage/column/version_info.*` for MVCC marker checks.
- `src/common/types.*` for `DataVector`, `DataChunk`, `Value`.

**External services/libraries:**
- None directly.

## Failure Modes and Edge Cases

- `ColumnChunk::WriteRow/WriteRows` throw `RuntimeError` for out-of-range offsets or unsorted offsets.
- `RowGroup::RevertAppend` assumes append touched pending data; segment-backed revert path is limited.
- Compression/decompression functions accept `buffer_size` arguments but many codec paths do not enforce tight bounds checks.
- String serialization in uncompressed and dictionary/WAL paths uses `uint16_t` lengths, truncating very long strings.

## Observability and Debugging

- Debug entry points:
- scan/append visibility behavior: `src/storage/column/row_group.cpp`
- segment read/write path: `src/storage/column/column_chunk.cpp`
- codec behavior: `src/storage/column/compression.cpp` and codec files under `src/storage/column/compression/`
- No built-in storage metrics.

## Risks and Notes

- `VersionInfo::AllInsertedVisibleTo` fast path improves scan performance but assumes marker counters remain consistent.
- Recompression on updates can be expensive for high-churn workloads due to segment-level rewrite cost.

Changes:

