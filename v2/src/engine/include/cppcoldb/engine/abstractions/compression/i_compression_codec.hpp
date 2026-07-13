#pragma once
#include <cstddef>
#include <cstdint>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"

namespace cppcoldb::engine::compression {

// Encodes/decodes a single column segment using one compression scheme
// (RLE, bit-packing, delta, dictionary, ...).
class ICompressionCodec {
public:
    virtual ~ICompressionCodec() = default;

    // Encode vec into dst_buffer (capacity buffer_size). Returns bytes written.
    virtual std::size_t Encode(const common::DataVector& vec,
                                std::uint8_t* dst_buffer, std::size_t buffer_size) const = 0;

    // Decode row_count rows of col_type from src_buffer into dst_vec.
    virtual void Decode(const std::uint8_t* src_buffer, std::size_t buffer_size,
                         std::size_t row_count, common::TypeId col_type,
                         common::DataVector& dst_vec) const = 0;
};

} // namespace cppcoldb::engine::compression
