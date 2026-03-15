#include "storage/column/compression/delta.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

size_t DeltaEncode(const int64_t* values, size_t row_count,
                   uint8_t* dst, size_t /*dst_size*/) {
    // Store first value as int64, subsequent deltas as int32.
    uint64_t first = static_cast<uint64_t>(values[0]);
    for (int i = 0; i < 8; ++i)
        dst[i] = static_cast<uint8_t>((first >> (8 * i)) & 0xFF);

    size_t pos = 8;
    for (size_t i = 1; i < row_count; ++i) {
        int32_t delta = static_cast<int32_t>(values[i] - values[i - 1]);
        uint32_t u = static_cast<uint32_t>(delta);
        dst[pos++] = static_cast<uint8_t>(u & 0xFF);
        dst[pos++] = static_cast<uint8_t>((u >> 8)  & 0xFF);
        dst[pos++] = static_cast<uint8_t>((u >> 16) & 0xFF);
        dst[pos++] = static_cast<uint8_t>((u >> 24) & 0xFF);
    }
    return pos;
}

void DeltaDecode(const uint8_t* src, size_t row_count, int64_t* values_out) {
    uint64_t first = 0;
    for (int i = 0; i < 8; ++i)
        first |= static_cast<uint64_t>(src[i]) << (8 * i);
    values_out[0] = static_cast<int64_t>(first);

    size_t pos = 8;
    for (size_t i = 1; i < row_count; ++i) {
        uint32_t u = 0;
        u |= static_cast<uint32_t>(src[pos++]);
        u |= static_cast<uint32_t>(src[pos++]) << 8;
        u |= static_cast<uint32_t>(src[pos++]) << 16;
        u |= static_cast<uint32_t>(src[pos++]) << 24;
        int32_t delta  = static_cast<int32_t>(u);
        values_out[i]  = values_out[i - 1] + delta;
    }
}

} // namespace cppcoldb
