#pragma once
#include <cstdint>

#include "cppcoldb/common/types/block_id.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/common/types/value.hpp"
#include "cppcoldb/engine/storage/compression/compression.hpp"

namespace cppcoldb::engine::storage {

// Per-segment statistics used for zone-map skip decisions.
struct SegmentStats {
    common::Value min_val; // INVALID type = uninitialized
    common::Value max_val;
    bool          has_nulls = false;
};

// Metadata for one compressed block of column data.
// The actual bytes live in the buffer manager block identified by block_id.
struct ColumnSegment {
    common::BlockId               block_id    = common::INVALID_BLOCK;
    compression::CompressionType  compression = compression::CompressionType::UNCOMPRESSED;
    common::TypeId                column_type = common::TypeId::INVALID;
    std::uint32_t                 row_count   = 0; // rows stored in this segment
    std::uint32_t                 row_offset  = 0; // starting row index within the RowGroup
    SegmentStats                  stats;
};

} // namespace cppcoldb::engine::storage
