#pragma once
#include <cstddef>
#include <cstdint>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/abstractions/compression/i_compression_codec.hpp"

namespace cppcoldb::engine::compression {

// Bit-packing codec for integer columns.
// Format: [min_val: int64][packed deltas: bit_width bits each, LSB-first].
// NULL rows store a 0 delta (ignored at decode via the validity bitmap).
class BitPackingCodec final : public ICompressionCodec {
public:
    BitPackingCodec() = default;
    ~BitPackingCodec() override = default;

    std::size_t Encode(const common::DataVector& vec,
                        std::uint8_t* dst_buffer, std::size_t buffer_size) const override;
    void Decode(const std::uint8_t* src_buffer, std::size_t buffer_size,
                std::size_t row_count, common::TypeId col_type,
                common::DataVector& dst_vec) const override;

    // Bit-level helpers (exported for testing).
    static std::size_t BitPackEncode(const std::int64_t* values, std::size_t row_count,
                                      std::uint8_t bit_width,
                                      std::uint8_t* dst, std::size_t dst_size);
    static void BitPackDecode(const std::uint8_t* src, std::size_t row_count,
                              std::uint8_t bit_width, std::int64_t* values_out);

    static void     WriteNBits(std::uint8_t* buf, std::size_t& bit_pos,
                                std::uint8_t width, std::uint64_t value);
    static std::uint64_t ReadNBits(const std::uint8_t* buf, std::size_t& bit_pos,
                                     std::uint8_t width);
};

} // namespace cppcoldb::engine::compression
