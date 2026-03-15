#pragma once
#include "common/types.hpp"
#include "storage/column/compression.hpp"

namespace cppcoldb {

// Per-segment statistics used for zone-map skip decisions.
struct SegmentStats {
    Value min_val;       // INVALID type = uninitialized
    Value max_val;
    bool  has_nulls = false;
};

// Metadata for one compressed block of column data.
// The actual bytes live in the BufferManager block identified by block_id.
struct ColumnSegment {
    BlockId         block_id    = INVALID_BLOCK;
    CompressionType compression = CompressionType::UNCOMPRESSED;
    TypeId          column_type = TypeId::INVALID;
    uint32_t        row_count   = 0;   // rows stored in this segment
    uint32_t        row_offset  = 0;   // starting row index within the RowGroup
    SegmentStats    stats;
};

} // namespace cppcoldb
