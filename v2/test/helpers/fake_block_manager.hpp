#pragma once
#include <cstddef>
#include <cstdint>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/storage/i_block_manager.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for IBlockManager. Not executed; every method throws.
class FakeBlockManager final : public engine::storage::IBlockManager {
public:
    void ReadBlock(common::BlockId id, std::uint8_t* buffer) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void WriteBlock(common::BlockId id, const std::uint8_t* buffer) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void Sync() override { CPPCOLDB_NOT_IMPLEMENTED(); }

    common::BlockId AllocateBlock() override { CPPCOLDB_NOT_IMPLEMENTED(); }

    std::size_t BlockSize()  const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    std::size_t BlockCount() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
