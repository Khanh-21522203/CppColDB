#include "cppcoldb/engine/storage/compression/delta.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::compression {

std::size_t DeltaCodec::Encode(const common::DataVector& /*vec*/,
                                std::uint8_t* /*dst_buffer*/, std::size_t /*buffer_size*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void DeltaCodec::Decode(const std::uint8_t* /*src_buffer*/, std::size_t /*buffer_size*/,
                         std::size_t /*row_count*/, common::TypeId /*col_type*/,
                         common::DataVector& /*dst_vec*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t DeltaCodec::DeltaEncode(const std::int64_t* /*values*/, std::size_t /*row_count*/,
                                     std::uint8_t* /*dst*/, std::size_t /*dst_size*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void DeltaCodec::DeltaDecode(const std::uint8_t* /*src*/, std::size_t /*row_count*/,
                              std::int64_t* /*values_out*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::compression
