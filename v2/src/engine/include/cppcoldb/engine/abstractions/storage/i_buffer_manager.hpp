#pragma once
#include <cstddef>
#include <memory>

#include "cppcoldb/common/types/block_id.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_handle.hpp"

namespace cppcoldb::engine::storage {

// Caches fixed-size blocks in memory with pin/unpin + LRU eviction, backed by
// an IBlockManager (or purely in-memory when constructed without one).
class IBufferManager {
public:
    virtual ~IBufferManager() = default;

    // Pin a block; loads it from the backing store if not already resident.
    virtual std::unique_ptr<IBufferHandle> Pin(common::BlockId id) = 0;

    // Mark a pinned block as modified.
    virtual void MarkDirty(common::BlockId id) = 0;

    // Allocate a new block (pinned, dirty, zero-filled).
    virtual std::unique_ptr<IBufferHandle> AllocateBlock() = 0;

    // Write all dirty blocks to the backing store.
    virtual void Flush() = 0;

    // Flush and release pool memory.
    virtual void Shutdown() = 0;

    virtual std::size_t BlockSize()  const = 0;
    virtual bool        IsInMemory() const = 0;
};

} // namespace cppcoldb::engine::storage
