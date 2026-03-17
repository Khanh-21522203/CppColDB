#pragma once
#include <vector>
#include "storage/column/column_segment.hpp"
#include "common/types.hpp"

namespace cppcoldb {

class BufferManager;

// Scan cursor for reading from a ColumnChunk.
struct ColumnScanState {
    size_t segment_idx    = 0; // current segment (segments_.size() = pending_data_)
    size_t row_in_segment = 0; // next row to read within current segment
};

// One column's worth of data within a RowGroup.
// Owns zero or more compressed ColumnSegments plus an in-memory pending buffer.
class ColumnChunk {
public:
    ColumnChunk(TypeId type, BufferManager& bm);

    // Build a scan cursor positioned at the given row offset within this chunk.
    ColumnScanState MakeScanState(size_t row_offset) const;

    // Read up to `count` rows starting at scan_state into output.
    // Returns the number of rows actually read.
    size_t Scan(ColumnScanState& state, size_t count, DataVector& output);

    // Compress pending_data_ into a new segment block in the BufferManager.
    // No-op if pending_data_ is empty.
    void Flush();

    // Append `count` rows from vec into pending_data_.
    void AppendFromVector(const DataVector& vec, size_t count);

    // Truncate pending_data_ to `new_count` rows (used by RevertAppend).
    void TruncatePending(size_t new_count);

    TypeId              type()            const { return type_; }
    size_t              RowCount()        const;
    size_t              SegmentRowCount() const;
    const SegmentStats& Stats()           const { return combined_stats_; }

    // For checkpoint serialization / deserialization.
    const std::vector<ColumnSegment>& Segments()           const { return segments_; }
    void AddSegment(ColumnSegment seg) { segments_.push_back(std::move(seg)); }

private:
    void UpdateCombinedStats(const DataVector& vec, size_t count);

    TypeId                     type_;
    BufferManager&             bm_;
    std::vector<ColumnSegment> segments_;
    DataVector                 pending_data_;
    size_t                     pending_row_offset_ = 0; // row_offset of first pending row
    SegmentStats               combined_stats_;
};

} // namespace cppcoldb
