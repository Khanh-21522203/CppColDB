#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "cppcoldb/common/types/block_id.hpp"
#include "cppcoldb/engine/abstractions/storage/i_block_manager.hpp"

namespace cppcoldb::engine::storage {

// Manages the single on-disk file that stores all column data blocks.
// Blocks are fixed-size and addressed by BlockId (= byte_offset / block_size).
class BlockFile final : public IBlockManager {
public:
    BlockFile(const std::string& path, std::size_t block_size);
    ~BlockFile() override = default;

    // Non-copyable, movable.
    BlockFile(const BlockFile&)            = delete;
    BlockFile& operator=(const BlockFile&) = delete;

    void ReadBlock(common::BlockId id, std::uint8_t* buffer) override;
    void WriteBlock(common::BlockId id, const std::uint8_t* buffer) override;
    void Sync() override;

    common::BlockId AllocateBlock() override;

    std::size_t BlockSize()  const override { return block_size_; }
    std::size_t BlockCount() const override { return block_count_; }

private:
    int         fd_ = -1;
    std::size_t block_size_  = 0;
    std::size_t block_count_ = 0;
};

} // namespace cppcoldb::engine::storage
