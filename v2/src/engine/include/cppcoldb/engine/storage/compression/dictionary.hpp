#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/abstractions/compression/i_compression_codec.hpp"

namespace cppcoldb::engine::compression {

// Dictionary encoding for VARCHAR columns.
// Format: [dict_size: uint32]
//         for each entry: [str_len: uint16][str bytes]
//         [codes: uint16 * row_count]  (0xFFFF = NULL)
class DictionaryCodec final : public ICompressionCodec {
public:
    DictionaryCodec() = default;
    ~DictionaryCodec() override = default;

    std::size_t Encode(const common::DataVector& vec,
                        std::uint8_t* dst_buffer, std::size_t buffer_size) const override;
    void Decode(const std::uint8_t* src_buffer, std::size_t buffer_size,
                std::size_t row_count, common::TypeId col_type,
                common::DataVector& dst_vec) const override;

    static std::size_t DictEncode(const std::string* values, std::size_t row_count,
                                   const std::uint8_t* validity_bitmap,
                                   std::uint8_t* dst, std::size_t dst_size);
    static void DictDecode(const std::uint8_t* src, std::size_t src_size, std::size_t row_count,
                            std::string* values_out, std::uint8_t* validity_bitmap_out);
};

} // namespace cppcoldb::engine::compression
