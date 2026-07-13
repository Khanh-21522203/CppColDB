#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "cppcoldb/common/constants.hpp"
#include "cppcoldb/common/types/block_id.hpp"
#include "cppcoldb/engine/abstractions/storage/i_block_manager.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_handle.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_manager.hpp"

namespace cppcoldb::engine::storage {

inline constexpr std::size_t DEFAULT_BUFFER_POOL_SIZE = 256ULL * 1024 * 1024; // 256 MiB

class BufferManager;

// One slot in the buffer pool.
struct Frame {
    std::unique_ptr<std::uint8_t[]>      buffer;
    std::int32_t                         pin_count = 0;
    bool                                  dirty     = false;
    std::list<common::BlockId>::iterator lru_it;
    bool                                  in_lru    = false;
};

// RAII handle returned by BufferManager::Pin(). Automatically unpins when destroyed.
class BufferHandle final : public IBufferHandle {
public:
    BufferHandle() = default;
    BufferHandle(BufferManager* bm, common::BlockId id, std::uint8_t* data);
    ~BufferHandle() override = default;

    BufferHandle(const BufferHandle&)            = delete;
    BufferHandle& operator=(const BufferHandle&) = delete;
    BufferHandle(BufferHandle&&) noexcept;
    BufferHandle& operator=(BufferHandle&&) noexcept;

    std::uint8_t*   Data()  const override { return data_; }
    common::BlockId Id()    const override { return block_id_; }
    bool            Valid() const override { return data_ != nullptr; }

    void MarkDirty() override;

private:
    BufferManager*  bm_       = nullptr;
    common::BlockId block_id_ = common::INVALID_BLOCK;
    std::uint8_t*   data_     = nullptr;
};

class BufferManager final : public IBufferManager {
public:
    // pool_bytes: total pool size; file: nullptr = in-memory mode.
    explicit BufferManager(std::size_t pool_bytes = DEFAULT_BUFFER_POOL_SIZE,
                            std::size_t block_size = common::DEFAULT_BLOCK_SIZE,
                            IBlockManager* file = nullptr);
    ~BufferManager() override = default;

    std::unique_ptr<IBufferHandle> Pin(common::BlockId id) override;
    void MarkDirty(common::BlockId id) override;
    std::unique_ptr<IBufferHandle> AllocateBlock() override;

    void Flush() override;
    void Shutdown() override;

    std::size_t BlockSize()  const override { return block_size_; }
    bool        IsInMemory() const override { return file_ == nullptr; }

private:
    void Unpin(common::BlockId id); // called by BufferHandle destructor
    friend class BufferHandle;

    void            EvictOne();
    void            EnsureSpace();
    common::BlockId NextBlockId();

    std::size_t    pool_bytes_;
    std::size_t    block_size_;
    std::size_t    used_bytes_ = 0;
    IBlockManager* file_;

    std::unordered_map<common::BlockId, Frame> pool_;
    std::list<common::BlockId>                 lru_list_; // back = LRU eviction candidate
    common::BlockId                            next_block_id_{0};

    std::mutex mu_;
};

} // namespace cppcoldb::engine::storage
