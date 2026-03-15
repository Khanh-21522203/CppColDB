#pragma once
#include <cstdint>
#include <cstddef>

namespace cppcoldb {

// Bit-packing for integer columns.
// Format: [min_val: int64][packed deltas: bit_width bits each, LSB-first]
// NULL rows store a 0 delta (ignored at decode via validity bitmap).
size_t BitPackEncode(const int64_t* values, size_t row_count,
                     uint8_t bit_width,
                     uint8_t* dst, size_t dst_size);

void BitPackDecode(const uint8_t* src, size_t row_count,
                   uint8_t bit_width, int64_t* values_out);

// Bit-level helpers (exported for testing).
void     WriteNBits(uint8_t* buf, size_t& bit_pos, uint8_t width, uint64_t value);
uint64_t ReadNBits (const uint8_t* buf, size_t& bit_pos, uint8_t width);

} // namespace cppcoldb
