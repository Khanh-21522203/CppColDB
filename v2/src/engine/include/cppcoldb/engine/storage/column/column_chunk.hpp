#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/common/types/value.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_manager.hpp"
#include "cppcoldb/engine/storage/column/column_segment.hpp"
#include "cppcoldb/engine/storage/scan_predicate.hpp"

namespace cppcoldb::engine::storage {

// Scan cursor for reading from a ColumnChunk.
struct ColumnScanState {
    std::size_t        segment_idx    = 0; // current segment (== segments_.size() means pending_data_)
    std::size_t        row_in_segment = 0; // next row to read within current segment
    common::DataVector scratch;            // reused across Scan() calls to avoid per-call heap alloc
};

// One column's worth of data within a RowGroup.
// Owns zero or more compressed ColumnSegments plus an in-memory pending buffer.
class ColumnChunk {
public:
    ColumnChunk(common::TypeId type, IBufferManager& bm);

    // Build a scan cursor positioned at the given row offset within this chunk.
    ColumnScanState MakeScanState(std::size_t row_offset) const;

    // Read up to `count` rows starting at scan_state into output.
    // Returns the number of rows actually read.
    std::size_t Scan(ColumnScanState& state, std::size_t count, common::DataVector& output);

    // Compress pending_data_ into a new segment block via the buffer manager.
    // No-op if pending_data_ is empty.
    void Flush();

    // Append `count` rows from vec into pending_data_.
    void AppendFromVector(const common::DataVector& vec, std::size_t count);

    // Truncate pending_data_ to `new_count` rows (used by RowGroup::RevertAppend).
    void TruncatePending(std::size_t new_count);

    // Overwrite a single row's value in-place.
    // Segment-backed rows are decompressed/recompressed into their existing block.
    // row_offset is the column-chunk-absolute row index.
    void WriteRow(std::uint32_t row_offset, const common::DataVector& src, std::size_t src_idx);

    // Overwrite multiple rows in one call.
    // `row_offsets.size()` must equal `src.count`; src row i is written to row_offsets[i].
    void WriteRows(const std::vector<std::uint32_t>& row_offsets, const common::DataVector& src);

    // Read specific rows by their column-chunk-absolute offsets into output.
    // offsets must be sorted ascending; each segment is decompressed at most once.
    void ScanRows(const std::vector<std::uint32_t>& offsets, common::DataVector& output);

    // Zone-map: if the segment containing row_offset is provably excluded by
    // (col op bound), returns the number of rows to skip (remaining rows in that
    // segment). Returns 0 if the segment cannot be skipped.
    std::size_t ZoneMapSkipRows(std::size_t row_offset, ScanPredicateOp op,
                                 const common::Value& bound) const;

    common::TypeId       type()            const { return type_; }
    std::size_t          RowCount()        const;
    std::size_t          SegmentRowCount() const;
    const SegmentStats&  Stats()           const { return combined_stats_; }

    // For checkpoint serialization / deserialization.
    const std::vector<ColumnSegment>& Segments() const { return segments_; }
    void AddSegment(ColumnSegment seg) { segments_.push_back(std::move(seg)); }

private:
    void UpdateCombinedStats(const common::DataVector& vec, std::size_t count);

    common::TypeId             type_;
    IBufferManager&            bm_;
    std::vector<ColumnSegment> segments_;
    common::DataVector         pending_data_;
    std::size_t                pending_row_offset_ = 0; // row_offset of the first pending row
    SegmentStats                combined_stats_;
};

} // namespace cppcoldb::engine::storage
