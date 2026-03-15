#pragma once
#include <string>
#include <cstdint>
#include "common/types.hpp"

namespace cppcoldb {

// Manages the single on-disk file that stores all column data blocks.
// Blocks are fixed-size and addressed by BlockId (= byte_offset / block_size).
class BlockFile {
public:
    explicit BlockFile(const std::string& path, size_t block_size);
    ~BlockFile();

    // Non-copyable, movable.
    BlockFile(const BlockFile&)            = delete;
    BlockFile& operator=(const BlockFile&) = delete;

    void ReadBlock(BlockId id, uint8_t* buffer);
    void WriteBlock(BlockId id, const uint8_t* buffer);
    void Sync(); // fsync

    // Extend the file by one block; return the new block's ID.
    BlockId AllocateBlock();

    size_t BlockSize()  const { return block_size_; }
    size_t BlockCount() const { return block_count_; }

private:
    int    fd_;
    size_t block_size_;
    size_t block_count_;
};

} // namespace cppcoldb
