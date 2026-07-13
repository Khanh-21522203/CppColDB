#pragma once
#include <cstddef>
#include <cstdint>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"

namespace cppcoldb::engine::compression {

enum class CompressionType : std::uint8_t {
    UNCOMPRESSED = 0,
    RLE          = 1,
    BIT_PACKED   = 2,
    DELTA        = 3,
    DICTIONARY   = 4,
};

// Segment block header (9 bytes, packed).
#pragma pack(push, 1)
struct SegmentHeader {
    std::uint8_t  compression_type;
    std::uint32_t row_count;
    std::uint32_t null_bitmap_size; // bytes used by the validity bitmap
};
#pragma pack(pop)
static_assert(sizeof(SegmentHeader) == 9, "SegmentHeader size changed");

// Result of compression analysis.
struct CompressionChoice {
    CompressionType type      = CompressionType::UNCOMPRESSED;
    std::uint8_t    bit_width = 0; // only for BIT_PACKED
};

// Analyze a DataVector and pick the best compression scheme.
CompressionChoice SelectCompression(const common::DataVector& vec);

// Compress vec into dst_buffer (must be at least buffer_size bytes).
// Returns the number of bytes written (including SegmentHeader + bitmap + data).
std::size_t Compress(CompressionChoice choice, const common::DataVector& vec,
                      std::uint8_t* dst_buffer, std::size_t buffer_size);

// Decompress src_buffer (starting with SegmentHeader) into dst_vec.
void Decompress(const std::uint8_t* src_buffer, std::size_t buffer_size,
                 common::TypeId col_type, common::DataVector& dst_vec);

} // namespace cppcoldb::engine::compression
