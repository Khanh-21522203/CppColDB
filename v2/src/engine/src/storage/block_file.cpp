#include "cppcoldb/engine/storage/block_file.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::storage {

BlockFile::BlockFile(const std::string& /*path*/, std::size_t block_size)
    : block_size_(block_size) {}

void BlockFile::ReadBlock(common::BlockId /*id*/, std::uint8_t* /*buffer*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void BlockFile::WriteBlock(common::BlockId /*id*/, const std::uint8_t* /*buffer*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void BlockFile::Sync() { CPPCOLDB_NOT_IMPLEMENTED(); }

common::BlockId BlockFile::AllocateBlock() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::storage
