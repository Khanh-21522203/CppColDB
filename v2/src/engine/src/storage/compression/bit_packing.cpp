#include "cppcoldb/engine/storage/compression/bit_packing.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::compression {

std::size_t BitPackingCodec::Encode(const common::DataVector& /*vec*/,
                                     std::uint8_t* /*dst_buffer*/, std::size_t /*buffer_size*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void BitPackingCodec::Decode(const std::uint8_t* /*src_buffer*/, std::size_t /*buffer_size*/,
                              std::size_t /*row_count*/, common::TypeId /*col_type*/,
                              common::DataVector& /*dst_vec*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::size_t BitPackingCodec::BitPackEncode(const std::int64_t* /*values*/, std::size_t /*row_count*/,
                                            std::uint8_t /*bit_width*/,
                                            std::uint8_t* /*dst*/, std::size_t /*dst_size*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void BitPackingCodec::BitPackDecode(const std::uint8_t* /*src*/, std::size_t /*row_count*/,
                                     std::uint8_t /*bit_width*/, std::int64_t* /*values_out*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void BitPackingCodec::WriteNBits(std::uint8_t* /*buf*/, std::size_t& /*bit_pos*/,
                                  std::uint8_t /*width*/, std::uint64_t /*value*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::uint64_t BitPackingCodec::ReadNBits(const std::uint8_t* /*buf*/, std::size_t& /*bit_pos*/,
                                          std::uint8_t /*width*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::compression
