#include "storage/column/compression/rle.hpp"
#include "common/exception.hpp"

#include <cstring>
#include <cstdio>

namespace cppcoldb {

static constexpr uint8_t TAG_NULL  = 0x00;
static constexpr uint8_t TAG_VALUE = 0x01;

// Helper: write uint32 to dst at pos (little-endian).
static void WriteU32At(uint8_t* dst, size_t& pos, uint32_t v) {
    dst[pos++] = static_cast<uint8_t>(v & 0xFF);
    dst[pos++] = static_cast<uint8_t>((v >> 8)  & 0xFF);
    dst[pos++] = static_cast<uint8_t>((v >> 16) & 0xFF);
    dst[pos++] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

static void WriteI64At(uint8_t* dst, size_t& pos, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        dst[pos++] = static_cast<uint8_t>((u >> (8 * i)) & 0xFF);
}

static uint32_t ReadU32At(const uint8_t* src, size_t& pos) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<uint32_t>(src[pos++]) << (8 * i);
    return v;
}

static int64_t ReadI64At(const uint8_t* src, size_t& pos) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i)
        u |= static_cast<uint64_t>(src[pos++]) << (8 * i);
    return static_cast<int64_t>(u);
}

static bool IsNull(const uint8_t* bitmap, size_t i) {
    return !(bitmap[i / 8] & (1u << (i % 8)));
}

size_t RleEncode(const int64_t* values, size_t row_count,
                 const uint8_t* validity_bitmap,
                 uint8_t* dst, size_t dst_size) {
    size_t out = 0;
    size_t i   = 0;

    while (i < row_count) {
        if (IsNull(validity_bitmap, i)) {
            // Count consecutive NULLs.
            size_t run = 0;
            while (i + run < row_count && IsNull(validity_bitmap, i + run)) ++run;
            // Write: [TAG_NULL][count: uint32]
            dst[out++] = TAG_NULL;
            WriteU32At(dst, out, static_cast<uint32_t>(run));
            i += run;
        } else {
            // Count consecutive equal non-NULL values.
            int64_t val = values[i];
            size_t run  = 1;
            while (i + run < row_count &&
                   !IsNull(validity_bitmap, i + run) &&
                   values[i + run] == val) {
                ++run;
            }
            // Write: [TAG_VALUE][value: int64][count: uint32]
            dst[out++] = TAG_VALUE;
            WriteI64At(dst, out, val);
            WriteU32At(dst, out, static_cast<uint32_t>(run));
            i += run;
        }
    }
    return out;
}

void RleDecode(const uint8_t* src, size_t /*src_size*/, size_t row_count,
               int64_t* values_out, uint8_t* validity_bitmap_out) {
    // Zero-init bitmap (all NULL).
    size_t bitmap_bytes = (row_count + 7) / 8;
    std::memset(validity_bitmap_out, 0, bitmap_bytes);

    size_t in   = 0;
    size_t out_i = 0;

    while (out_i < row_count) {
        uint8_t tag = src[in++];
        if (tag == TAG_NULL) {
            uint32_t count = ReadU32At(src, in);
            // Leave bitmap bits as 0 (NULL) for these rows.
            out_i += count;
        } else {
            int64_t  val   = ReadI64At(src, in);
            uint32_t count = ReadU32At(src, in);
            for (uint32_t k = 0; k < count && out_i < row_count; ++k, ++out_i) {
                values_out[out_i] = val;
                // Set NOT NULL bit.
                validity_bitmap_out[out_i / 8] |= static_cast<uint8_t>(1u << (out_i % 8));
            }
        }
    }
}

} // namespace cppcoldb
