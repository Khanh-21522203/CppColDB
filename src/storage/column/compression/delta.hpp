#pragma once
#include <cstdint>
#include <cstddef>

namespace cppcoldb {

// Delta encoding for monotonically non-decreasing integer columns.
// Format: [first_value: int64][delta_1: int32][delta_2: int32]...
// Returns bytes written.
size_t DeltaEncode(const int64_t* values, size_t row_count,
                   uint8_t* dst, size_t dst_size);

void DeltaDecode(const uint8_t* src, size_t row_count, int64_t* values_out);

} // namespace cppcoldb
