#pragma once
#include <cstddef>
#include <cstdint>

#include "cppcoldb/common/types/block_id.hpp"

namespace cppcoldb::engine::storage {

// Manages the fixed-size block storage backing a single on-disk data file.
// Blocks are addressed by BlockId; implementations own the raw file handle.
class IBlockManager {
public:
    virtual ~IBlockManager() = default;

    virtual void ReadBlock(common::BlockId id, std::uint8_t* buffer) = 0;
    virtual void WriteBlock(common::BlockId id, const std::uint8_t* buffer) = 0;
    virtual void Sync() = 0;

    // Extend the file by one block; returns the new block's ID.
    virtual common::BlockId AllocateBlock() = 0;

    virtual std::size_t BlockSize()  const = 0;
    virtual std::size_t BlockCount() const = 0;
};

} // namespace cppcoldb::engine::storage
