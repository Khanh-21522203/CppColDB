#pragma once
#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>
#include <cstdint>
#include "common/types.hpp"

namespace cppcoldb {

static constexpr size_t DEFAULT_BLOCK_SIZE       = 256 * 1024;        // 256 KiB
static constexpr size_t DEFAULT_BUFFER_POOL_SIZE = 256ULL * 1024 * 1024; // 256 MiB

class BlockFile;

// One slot in the buffer pool.
struct Frame {
    std::unique_ptr<uint8_t[]>   buffer;
    int32_t                      pin_count = 0;
    bool                         dirty     = false;
    std::list<BlockId>::iterator lru_it;
    bool                         in_lru    = false;
};

// RAII handle returned by BufferManager::Pin().
// Automatically unpins when destroyed.
class BufferHandle {
public:
    BufferHandle() = default;
    BufferHandle(class BufferManager* bm, BlockId id, uint8_t* data);
    ~BufferHandle();

    BufferHandle(const BufferHandle&)            = delete;
    BufferHandle& operator=(const BufferHandle&) = delete;
    BufferHandle(BufferHandle&&) noexcept;
    BufferHandle& operator=(BufferHandle&&) noexcept;

    uint8_t* Data()  const { return data_; }
    BlockId  Id()    const { return block_id_; }
    bool     Valid() const { return data_ != nullptr; }

    // Convenience: marks this block dirty via the manager.
    void MarkDirty();

private:
    BufferManager* bm_       = nullptr;
    BlockId        block_id_ = INVALID_BLOCK;
    uint8_t*       data_     = nullptr;
};

class BufferManager {
public:
    // pool_bytes: total pool size; file: nullptr = in-memory mode.
    explicit BufferManager(size_t pool_bytes = DEFAULT_BUFFER_POOL_SIZE,
                           size_t block_size = DEFAULT_BLOCK_SIZE,
                           BlockFile* file   = nullptr);
    ~BufferManager();

    // Pin a block; loads from disk if not in pool.
    BufferHandle Pin(BlockId id);

    // Mark a pinned block as modified.
    void MarkDirty(BlockId id);

    // Allocate a new block (pinned, dirty, zero-filled).
    BufferHandle AllocateBlock();

    // Write all dirty blocks to disk.
    void Flush();

    // Flush and release pool memory.
    void Shutdown();

    size_t BlockSize()  const { return block_size_; }
    bool   IsInMemory() const { return file_ == nullptr; }

private:
    void    Unpin(BlockId id); // called by BufferHandle destructor
    friend class BufferHandle;

    void    EvictOne();
    void    EnsureSpace();
    BlockId NextBlockId();

    size_t                             pool_bytes_;
    size_t                             block_size_;
    size_t                             used_bytes_ = 0;
    BlockFile*                         file_;

    std::unordered_map<BlockId, Frame> pool_;
    std::list<BlockId>                 lru_list_; // back = LRU eviction candidate
    BlockId                            next_block_id_ = 0;

    std::mutex                         mu_;
};

} // namespace cppcoldb
