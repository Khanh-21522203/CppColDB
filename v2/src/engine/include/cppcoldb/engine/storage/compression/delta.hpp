#pragma once
#include <cstddef>
#include <cstdint>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/abstractions/compression/i_compression_codec.hpp"

namespace cppcoldb::engine::compression {

// Delta encoding for monotonically non-decreasing integer columns.
// Format: [first_value: int64][delta_1: int32][delta_2: int32]...
class DeltaCodec final : public ICompressionCodec {
public:
    DeltaCodec() = default;
    ~DeltaCodec() override = default;

    std::size_t Encode(const common::DataVector& vec,
                        std::uint8_t* dst_buffer, std::size_t buffer_size) const override;
    void Decode(const std::uint8_t* src_buffer, std::size_t buffer_size,
                std::size_t row_count, common::TypeId col_type,
                common::DataVector& dst_vec) const override;

    // Returns bytes written.
    static std::size_t DeltaEncode(const std::int64_t* values, std::size_t row_count,
                                    std::uint8_t* dst, std::size_t dst_size);
    static void DeltaDecode(const std::uint8_t* src, std::size_t row_count, std::int64_t* values_out);
};

} // namespace cppcoldb::engine::compression
