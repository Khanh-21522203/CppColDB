#pragma once
#include <cstddef>
#include <cstdint>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/abstractions/compression/i_compression_codec.hpp"

namespace cppcoldb::engine::compression {

// RLE for integer columns.
// Format: stream of runs — each run is either:
//   [0x00: uint8][count: uint32]               -> NULL run
//   [0x01: uint8][value: int64][count: uint32]  -> value run
class RleCodec final : public ICompressionCodec {
public:
    RleCodec() = default;
    ~RleCodec() override = default;

    std::size_t Encode(const common::DataVector& vec,
                        std::uint8_t* dst_buffer, std::size_t buffer_size) const override;
    void Decode(const std::uint8_t* src_buffer, std::size_t buffer_size,
                std::size_t row_count, common::TypeId col_type,
                common::DataVector& dst_vec) const override;

    static std::size_t RleEncode(const std::int64_t* values, std::size_t row_count,
                                  const std::uint8_t* validity_bitmap,
                                  std::uint8_t* dst, std::size_t dst_size);
    static void RleDecode(const std::uint8_t* src, std::size_t src_size, std::size_t row_count,
                           std::int64_t* values_out, std::uint8_t* validity_bitmap_out);
};

} // namespace cppcoldb::engine::compression
