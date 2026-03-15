#include "storage/buffer_manager.hpp"
#include "storage/block_file.hpp"
#include "common/exception.hpp"

#include <cstring>
#include <stdexcept>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// BufferHandle
// ---------------------------------------------------------------------------

BufferHandle::BufferHandle(BufferManager* bm, BlockId id, uint8_t* data)
    : bm_(bm), block_id_(id), data_(data) {}

BufferHandle::~BufferHandle() {
    if (bm_ && data_) {
        bm_->Unpin(block_id_);
    }
}

BufferHandle::BufferHandle(BufferHandle&& o) noexcept
    : bm_(o.bm_), block_id_(o.block_id_), data_(o.data_) {
    o.bm_   = nullptr;
    o.data_ = nullptr;
}

BufferHandle& BufferHandle::operator=(BufferHandle&& o) noexcept {
    if (this != &o) {
        if (bm_ && data_) bm_->Unpin(block_id_);
        bm_      = o.bm_;
        block_id_= o.block_id_;
        data_    = o.data_;
        o.bm_    = nullptr;
        o.data_  = nullptr;
    }
    return *this;
}

void BufferHandle::MarkDirty() {
    if (bm_ && data_) bm_->MarkDirty(block_id_);
}

// ---------------------------------------------------------------------------
// BufferManager
// ---------------------------------------------------------------------------

BufferManager::BufferManager(size_t pool_bytes, size_t block_size, BlockFile* file)
    : pool_bytes_(pool_bytes), block_size_(block_size), file_(file) {}

BufferManager::~BufferManager() {
    Shutdown();
}

void BufferManager::Unpin(BlockId id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = pool_.find(id);
    if (it == pool_.end()) return;
    Frame& frame = it->second;
    --frame.pin_count;
    if (frame.pin_count == 0) {
        lru_list_.push_front(id);
        frame.lru_it = lru_list_.begin();
        frame.in_lru = true;
    }
}

void BufferManager::EvictOne() {
    // Caller must hold mu_.
    if (lru_list_.empty())
        throw RuntimeError("BufferManager: all blocks pinned, cannot evict");

    BlockId victim = lru_list_.back();
    lru_list_.pop_back();

    Frame& frame = pool_.at(victim);
    if (frame.dirty && file_) {
        file_->WriteBlock(victim, frame.buffer.get());
        frame.dirty = false;
    }
    pool_.erase(victim);
    used_bytes_ -= block_size_;
}

void BufferManager::EnsureSpace() {
    // Caller must hold mu_.
    while (used_bytes_ + block_size_ > pool_bytes_) {
        EvictOne();
    }
}

BufferHandle BufferManager::Pin(BlockId id) {
    std::lock_guard<std::mutex> lock(mu_);

    auto it = pool_.find(id);
    if (it != pool_.end()) {
        Frame& frame = it->second;
        if (frame.pin_count == 0 && frame.in_lru) {
            lru_list_.erase(frame.lru_it);
            frame.in_lru = false;
        }
        ++frame.pin_count;
        return BufferHandle(this, id, frame.buffer.get());
    }

    // Block not in pool — load it.
    EnsureSpace();

    Frame frame;
    frame.buffer    = std::make_unique<uint8_t[]>(block_size_);
    frame.pin_count = 1;
    frame.dirty     = false;
    frame.in_lru    = false;

    if (file_) {
        file_->ReadBlock(id, frame.buffer.get());
    } else {
        std::memset(frame.buffer.get(), 0, block_size_);
    }

    uint8_t* ptr = frame.buffer.get();
    pool_.emplace(id, std::move(frame));
    used_bytes_ += block_size_;
    return BufferHandle(this, id, ptr);
}

void BufferManager::MarkDirty(BlockId id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = pool_.find(id);
    if (it != pool_.end()) it->second.dirty = true;
}

BufferHandle BufferManager::AllocateBlock() {
    std::lock_guard<std::mutex> lock(mu_);

    BlockId id = next_block_id_++;
    if (file_) file_->AllocateBlock(); // extend file

    EnsureSpace();

    Frame frame;
    frame.buffer    = std::make_unique<uint8_t[]>(block_size_);
    frame.pin_count = 1;
    frame.dirty     = true;
    frame.in_lru    = false;
    std::memset(frame.buffer.get(), 0, block_size_);

    uint8_t* ptr = frame.buffer.get();
    pool_.emplace(id, std::move(frame));
    used_bytes_ += block_size_;
    return BufferHandle(this, id, ptr);
}

void BufferManager::Flush() {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto& [id, frame] : pool_) {
        if (frame.dirty && file_) {
            file_->WriteBlock(id, frame.buffer.get());
            frame.dirty = false;
        }
    }
    if (file_) file_->Sync();
}

void BufferManager::Shutdown() {
    Flush();
    std::lock_guard<std::mutex> lock(mu_);
    pool_.clear();
    lru_list_.clear();
    used_bytes_ = 0;
}

} // namespace cppcoldb
