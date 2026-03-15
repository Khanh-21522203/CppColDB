#include "storage/column/compression/dictionary.hpp"
#include "common/exception.hpp"

#include <unordered_map>
#include <vector>
#include <cstring>

namespace cppcoldb {

static constexpr uint16_t NULL_CODE = 0xFFFF;

static bool IsNull(const uint8_t* bitmap, size_t i) {
    return !(bitmap[i / 8] & (1u << (i % 8)));
}

static void WriteU16At(uint8_t* dst, size_t& pos, uint16_t v) {
    dst[pos++] = static_cast<uint8_t>(v & 0xFF);
    dst[pos++] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

static void WriteU32At(uint8_t* dst, size_t& pos, uint32_t v) {
    dst[pos++] = static_cast<uint8_t>(v & 0xFF);
    dst[pos++] = static_cast<uint8_t>((v >> 8)  & 0xFF);
    dst[pos++] = static_cast<uint8_t>((v >> 16) & 0xFF);
    dst[pos++] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

static uint16_t ReadU16At(const uint8_t* src, size_t& pos) {
    uint16_t v = static_cast<uint16_t>(src[pos]) |
                 static_cast<uint16_t>(static_cast<uint16_t>(src[pos + 1]) << 8);
    pos += 2;
    return v;
}

static uint32_t ReadU32At(const uint8_t* src, size_t& pos) {
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(src[pos++]) << (8 * i);
    return v;
}

size_t DictEncode(const std::string* values, size_t row_count,
                  const uint8_t* validity_bitmap,
                  uint8_t* dst, size_t /*dst_size*/) {
    // Build dictionary: string → code (uint16).
    std::unordered_map<std::string, uint16_t> dict;
    std::vector<std::string> dict_ordered; // code → string

    std::vector<uint16_t> codes(row_count);
    for (size_t i = 0; i < row_count; ++i) {
        if (IsNull(validity_bitmap, i)) {
            codes[i] = NULL_CODE;
        } else {
            auto it = dict.find(values[i]);
            if (it == dict.end()) {
                uint16_t code = static_cast<uint16_t>(dict.size());
                dict[values[i]] = code;
                dict_ordered.push_back(values[i]);
                codes[i] = code;
            } else {
                codes[i] = it->second;
            }
        }
    }

    size_t pos = 0;

    // Write dict_size.
    WriteU32At(dst, pos, static_cast<uint32_t>(dict_ordered.size()));

    // Write each dictionary entry: [str_len: uint16][str bytes].
    for (const auto& s : dict_ordered) {
        WriteU16At(dst, pos, static_cast<uint16_t>(s.size()));
        std::memcpy(dst + pos, s.data(), s.size());
        pos += s.size();
    }

    // Write codes array.
    for (uint16_t code : codes) WriteU16At(dst, pos, code);

    return pos;
}

void DictDecode(const uint8_t* src, size_t /*src_size*/, size_t row_count,
                std::string* values_out, uint8_t* validity_bitmap_out) {
    size_t bitmap_bytes = (row_count + 7) / 8;
    std::memset(validity_bitmap_out, 0, bitmap_bytes);

    size_t pos = 0;

    // Read dictionary.
    uint32_t dict_size = ReadU32At(src, pos);
    std::vector<std::string> dict(dict_size);
    for (uint32_t i = 0; i < dict_size; ++i) {
        uint16_t str_len = ReadU16At(src, pos);
        dict[i].assign(reinterpret_cast<const char*>(src + pos), str_len);
        pos += str_len;
    }

    // Decode codes.
    for (size_t i = 0; i < row_count; ++i) {
        uint16_t code = ReadU16At(src, pos);
        if (code == NULL_CODE) {
            // Leave bitmap bit as 0 (NULL).
        } else {
            values_out[i] = dict[code];
            validity_bitmap_out[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
        }
    }
}

} // namespace cppcoldb
