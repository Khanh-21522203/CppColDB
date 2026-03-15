#pragma once
#include <string>
#include <cstdint>
#include <cstddef>

namespace cppcoldb {

// Dictionary encoding for VARCHAR columns.
// Format: [dict_size: uint32]
//         for each entry: [str_len: uint16][str bytes]
//         [codes: uint16 * row_count]  (0xFFFF = NULL)
size_t DictEncode(const std::string* values, size_t row_count,
                  const uint8_t* validity_bitmap,
                  uint8_t* dst, size_t dst_size);

void DictDecode(const uint8_t* src, size_t src_size, size_t row_count,
                std::string* values_out, uint8_t* validity_bitmap_out);

} // namespace cppcoldb
