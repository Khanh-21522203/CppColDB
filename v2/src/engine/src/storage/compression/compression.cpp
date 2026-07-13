#include "cppcoldb/engine/storage/compression/compression.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::compression {

CompressionChoice SelectCompression(const common::DataVector& /*vec*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

std::size_t Compress(CompressionChoice /*choice*/, const common::DataVector& /*vec*/,
                      std::uint8_t* /*dst_buffer*/, std::size_t /*buffer_size*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Decompress(const std::uint8_t* /*src_buffer*/, std::size_t /*buffer_size*/,
                 common::TypeId /*col_type*/, common::DataVector& /*dst_vec*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::compression
