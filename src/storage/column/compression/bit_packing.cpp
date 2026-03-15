#include "storage/column/compression/bit_packing.hpp"
#include "common/exception.hpp"

#include <cstring>
#include <climits>
#include <algorithm>

namespace cppcoldb {

void WriteNBits(uint8_t* buf, size_t& bit_pos, uint8_t width, uint64_t value) {
    size_t remaining = width;
    while (remaining > 0) {
        size_t byte_idx      = bit_pos / 8;
        size_t bit_in_byte   = bit_pos % 8;
        size_t bits_avail    = 8 - bit_in_byte;
        size_t bits_to_write = std::min(remaining, bits_avail);

        uint64_t mask  = (1ULL << bits_to_write) - 1;
        uint64_t chunk = (value >> (width - remaining)) & mask;
        buf[byte_idx] |= static_cast<uint8_t>(chunk << bit_in_byte);

        bit_pos   += bits_to_write;
        remaining -= bits_to_write;
    }
}

uint64_t ReadNBits(const uint8_t* buf, size_t& bit_pos, uint8_t width) {
    uint64_t result    = 0;
    size_t   bits_read = 0;
    while (bits_read < width) {
        size_t byte_idx    = bit_pos / 8;
        size_t bit_in_byte = bit_pos % 8;
        size_t bits_avail  = 8 - bit_in_byte;
        size_t to_read     = std::min(static_cast<size_t>(width) - bits_read, bits_avail);

        uint64_t mask  = (1ULL << to_read) - 1;
        uint64_t chunk = (static_cast<uint64_t>(buf[byte_idx]) >> bit_in_byte) & mask;
        result |= chunk << bits_read;

        bit_pos   += to_read;
        bits_read += to_read;
    }
    return result;
}

size_t BitPackEncode(const int64_t* values, size_t row_count,
                     uint8_t bit_width,
                     uint8_t* dst, size_t /*dst_size*/) {
    // Find minimum value for offset encoding.
    int64_t min_val = values[0];
    for (size_t i = 1; i < row_count; ++i)
        if (values[i] < min_val) min_val = values[i];

    // Write min_val as first 8 bytes (little-endian int64).
    uint64_t min_u = static_cast<uint64_t>(min_val);
    for (int i = 0; i < 8; ++i)
        dst[i] = static_cast<uint8_t>((min_u >> (8 * i)) & 0xFF);

    // Pack deltas.
    size_t bit_pos = 64; // start after 8-byte min_val
    std::memset(dst + 8, 0, ((bit_width * row_count + 7) / 8 + 1)); // zero the bit area
    for (size_t i = 0; i < row_count; ++i) {
        uint64_t delta = static_cast<uint64_t>(values[i] - min_val);
        WriteNBits(dst, bit_pos, bit_width, delta);
    }
    return (bit_pos + 7) / 8;
}

void BitPackDecode(const uint8_t* src, size_t row_count,
                   uint8_t bit_width, int64_t* values_out) {
    // Read min_val.
    uint64_t min_u = 0;
    for (int i = 0; i < 8; ++i)
        min_u |= static_cast<uint64_t>(src[i]) << (8 * i);
    int64_t min_val = static_cast<int64_t>(min_u);

    size_t bit_pos = 64;
    for (size_t i = 0; i < row_count; ++i) {
        uint64_t delta  = ReadNBits(src, bit_pos, bit_width);
        values_out[i]   = min_val + static_cast<int64_t>(delta);
    }
}

} // namespace cppcoldb
