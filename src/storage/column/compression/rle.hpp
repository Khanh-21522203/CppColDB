#pragma once
#include <cstdint>
#include <cstddef>

namespace cppcoldb {

// RLE for integer columns.
// Format: stream of runs — each run is either:
//   [0x00: uint8][count: uint32]               → NULL run
//   [0x01: uint8][value: int64][count: uint32]  → value run
size_t RleEncode(const int64_t* values, size_t row_count,
                 const uint8_t* validity_bitmap,
                 uint8_t* dst, size_t dst_size);

void RleDecode(const uint8_t* src, size_t src_size, size_t row_count,
               int64_t* values_out, uint8_t* validity_bitmap_out);

} // namespace cppcoldb
