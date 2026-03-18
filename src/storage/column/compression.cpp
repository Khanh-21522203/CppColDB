#include "storage/column/compression.hpp"
#include "storage/column/compression/rle.hpp"
#include "storage/column/compression/bit_packing.hpp"
#include "storage/column/compression/delta.hpp"
#include "storage/column/compression/dictionary.hpp"
#include "common/exception.hpp"

#include <unordered_set>
#include <cstring>
#include <cmath>
#include <climits>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Analysis helpers
// ---------------------------------------------------------------------------

static bool IsIntType(TypeId t) {
    return t == TypeId::INT8 || t == TypeId::INT16 || t == TypeId::INT32 ||
           t == TypeId::INT64 || t == TypeId::BOOLEAN;
}

static bool IsFloatType(TypeId t) {
    return t == TypeId::FLOAT32 || t == TypeId::FLOAT64;
}

static bool AllIdentical(const DataVector& vec) {
    int64_t first = 0;
    bool    found = false;
    for (size_t i = 0; i < vec.count; ++i) {
        if (vec.IsNull(i)) continue;
        if (!found) { first = vec.int_data[i]; found = true; }
        else if (vec.int_data[i] != first) return false;
    }
    return true;
}

static uint8_t MinBitsRequired(const DataVector& vec) {
    int64_t mn = INT64_MAX, mx = INT64_MIN;
    for (size_t i = 0; i < vec.count; ++i) {
        if (vec.IsNull(i)) continue;
        if (vec.int_data[i] < mn) mn = vec.int_data[i];
        if (vec.int_data[i] > mx) mx = vec.int_data[i];
    }
    if (mn == INT64_MAX) return 0; // all NULL
    uint64_t range = static_cast<uint64_t>(mx - mn);
    if (range == 0) return 1;
    uint8_t bits = 0;
    while ((1ULL << bits) <= range) ++bits;
    return bits;
}

static bool IsMonotonic(const DataVector& vec) {
    int64_t prev = INT64_MIN;
    for (size_t i = 0; i < vec.count; ++i) {
        if (vec.IsNull(i)) continue;
        if (vec.int_data[i] < prev) return false;
        prev = vec.int_data[i];
    }
    return true;
}

static size_t CountDistinct(const DataVector& vec) {
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < vec.count; ++i) {
        if (!vec.IsNull(i)) seen.insert(vec.str_data[i]);
    }
    return seen.size();
}

// ---------------------------------------------------------------------------
// SelectCompression
// ---------------------------------------------------------------------------

CompressionChoice SelectCompression(const DataVector& vec) {
    if (vec.count == 0) return {CompressionType::UNCOMPRESSED, 0};

    if (IsIntType(vec.type)) {
        if (AllIdentical(vec))             return {CompressionType::RLE, 0};
        uint8_t bits = MinBitsRequired(vec);
        if (bits <= 32)                    return {CompressionType::BIT_PACKED, bits};
        if (IsMonotonic(vec))              return {CompressionType::DELTA, 0};
        return {CompressionType::UNCOMPRESSED, 0};
    }

    if (IsFloatType(vec.type))             return {CompressionType::UNCOMPRESSED, 0};

    if (vec.type == TypeId::VARCHAR) {
        if (CountDistinct(vec) <= 1000)    return {CompressionType::DICTIONARY, 0};
        return {CompressionType::UNCOMPRESSED, 0};
    }

    return {CompressionType::UNCOMPRESSED, 0};
}

// ---------------------------------------------------------------------------
// Validity bitmap helpers
// ---------------------------------------------------------------------------

static size_t BitmapBytes(size_t row_count) {
    return (row_count + 7) / 8;
}

static void BuildBitmap(const DataVector& vec, uint8_t* bitmap) {
    size_t bytes = BitmapBytes(vec.count);
    std::memset(bitmap, 0, bytes);
    for (size_t i = 0; i < vec.count; ++i) {
        if (!vec.IsNull(i))
            bitmap[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    }
}

static void ApplyBitmap(const uint8_t* bitmap, DataVector& vec) {
    for (size_t i = 0; i < vec.count; ++i) {
        if (bitmap[i / 8] & (1u << (i % 8)))
            vec.validity.set(i);
        else
            vec.validity.reset(i);
    }
}

// ---------------------------------------------------------------------------
// Compress
// ---------------------------------------------------------------------------

size_t Compress(CompressionChoice choice, const DataVector& vec,
                uint8_t* dst_buffer, size_t buffer_size) {
    size_t row_count   = vec.count;
    size_t bitmap_size = BitmapBytes(row_count);

    // Reserve space for SegmentHeader (9 bytes) + bitmap.
    constexpr size_t HDR_SIZE = sizeof(SegmentHeader);
    uint8_t* bitmap_ptr = dst_buffer + HDR_SIZE;
    uint8_t* data_ptr   = bitmap_ptr + bitmap_size;
    size_t   data_avail = buffer_size - HDR_SIZE - bitmap_size;

    BuildBitmap(vec, bitmap_ptr);

    size_t data_bytes = 0;

    switch (choice.type) {
        case CompressionType::UNCOMPRESSED: {
            if (IsIntType(vec.type)) {
                for (size_t i = 0; i < row_count; ++i) {
                    int64_t v = vec.IsNull(i) ? int64_t{0} : vec.int_data[i];
                    std::memcpy(data_ptr + data_bytes, &v, 8);
                    data_bytes += 8;
                }
            } else if (IsFloatType(vec.type)) {
                for (size_t i = 0; i < row_count; ++i) {
                    double d = vec.IsNull(i) ? 0.0 : vec.float_data[i];
                    std::memcpy(data_ptr + data_bytes, &d, 8);
                    data_bytes += 8;
                }
            } else { // VARCHAR
                for (size_t i = 0; i < row_count; ++i) {
                    const std::string& s = vec.IsNull(i) ? std::string{} : vec.str_data[i];
                    uint16_t len = static_cast<uint16_t>(s.size());
                    data_ptr[data_bytes++] = len & 0xFF;
                    data_ptr[data_bytes++] = (len >> 8) & 0xFF;
                    std::memcpy(data_ptr + data_bytes, s.data(), len);
                    data_bytes += len;
                }
            }
            break;
        }
        case CompressionType::RLE: {
            data_bytes = RleEncode(vec.int_data.data(), row_count,
                                   bitmap_ptr, data_ptr, data_avail);
            break;
        }
        case CompressionType::BIT_PACKED: {
            // Prepend bit_width as first byte so Decompress can read it back.
            data_ptr[data_bytes++] = choice.bit_width;
            std::vector<int64_t> tmp(row_count, 0);
            for (size_t i = 0; i < row_count; ++i)
                if (!vec.IsNull(i)) tmp[i] = vec.int_data[i];
            data_bytes += BitPackEncode(tmp.data(), row_count,
                                        choice.bit_width, data_ptr + data_bytes,
                                        data_avail - data_bytes);
            break;
        }
        case CompressionType::DELTA: {
            std::vector<int64_t> tmp(row_count, 0);
            for (size_t i = 0; i < row_count; ++i)
                if (!vec.IsNull(i)) tmp[i] = vec.int_data[i];
            data_bytes = DeltaEncode(tmp.data(), row_count, data_ptr, data_avail);
            break;
        }
        case CompressionType::DICTIONARY: {
            data_bytes = DictEncode(vec.str_data.data(), row_count,
                                    bitmap_ptr, data_ptr, data_avail);
            break;
        }
    }

    // Write SegmentHeader at the start.
    SegmentHeader hdr{};
    hdr.compression_type = static_cast<uint8_t>(choice.type);
    hdr.row_count        = static_cast<uint32_t>(row_count);
    hdr.null_bitmap_size = static_cast<uint32_t>(bitmap_size);
    std::memcpy(dst_buffer, &hdr, HDR_SIZE);

    return HDR_SIZE + bitmap_size + data_bytes;
}

// ---------------------------------------------------------------------------
// Decompress
// ---------------------------------------------------------------------------

void Decompress(const uint8_t* src_buffer, size_t buffer_size,
                TypeId col_type, DataVector& dst_vec) {
    constexpr size_t HDR_SIZE = sizeof(SegmentHeader);

    SegmentHeader hdr{};
    std::memcpy(&hdr, src_buffer, HDR_SIZE);

    size_t row_count   = hdr.row_count;
    size_t bitmap_size = hdr.null_bitmap_size;

    const uint8_t* bitmap_ptr = src_buffer + HDR_SIZE;
    const uint8_t* data_ptr   = bitmap_ptr + bitmap_size;
    size_t         data_size  = buffer_size - HDR_SIZE - bitmap_size;

    auto type = static_cast<CompressionType>(hdr.compression_type);

    dst_vec.Reset(col_type, row_count);

    switch (type) {
        case CompressionType::UNCOMPRESSED: {
            if (IsIntType(col_type)) {
                std::memcpy(dst_vec.int_data.data(), data_ptr, row_count * 8);
            } else if (IsFloatType(col_type)) {
                std::memcpy(dst_vec.float_data.data(), data_ptr, row_count * 8);
            } else { // VARCHAR
                size_t pos = 0;
                for (size_t i = 0; i < row_count; ++i) {
                    uint16_t len = static_cast<uint16_t>(data_ptr[pos]) |
                                   static_cast<uint16_t>(static_cast<uint16_t>(data_ptr[pos + 1]) << 8);
                    pos += 2;
                    dst_vec.str_data[i].assign(reinterpret_cast<const char*>(data_ptr + pos), len);
                    pos += len;
                }
            }
            break;
        }
        case CompressionType::RLE: {
            std::vector<uint8_t> rle_bitmap(bitmap_size, 0);
            RleDecode(data_ptr, data_size, row_count,
                      dst_vec.int_data.data(), rle_bitmap.data());
            ApplyBitmap(rle_bitmap.data(), dst_vec);
            return; // bitmap applied inside; skip ApplyBitmap below
        }
        case CompressionType::BIT_PACKED: {
            // Read bit_width from SegmentHeader... but SegmentHeader doesn't store it.
            // Derive it: bit_width is stored implicitly from how data was written.
            // Actually we need to store bit_width. Use a convention: first byte of data = bit_width.
            // Re-check plan... The plan says bit_width is in CompressionChoice.
            // Since SegmentHeader doesn't store bit_width, let's pack it as first data byte.
            uint8_t bit_width = data_ptr[0];
            BitPackDecode(data_ptr + 1, row_count, bit_width, dst_vec.int_data.data());
            break;
        }
        case CompressionType::DELTA: {
            DeltaDecode(data_ptr, row_count, dst_vec.int_data.data());
            break;
        }
        case CompressionType::DICTIONARY: {
            std::vector<uint8_t> dict_bitmap(bitmap_size, 0);
            DictDecode(data_ptr, data_size, row_count,
                       dst_vec.str_data.data(), dict_bitmap.data());
            ApplyBitmap(dict_bitmap.data(), dst_vec);
            return;
        }
    }

    ApplyBitmap(bitmap_ptr, dst_vec);
}

} // namespace cppcoldb
