#include "cppcoldb/engine/storage/buffer_manager.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::storage {

BufferHandle::BufferHandle(BufferManager* bm, common::BlockId id, std::uint8_t* data)
    : bm_(bm), block_id_(id), data_(data) {}

BufferHandle::BufferHandle(BufferHandle&&) noexcept { CPPCOLDB_NOT_IMPLEMENTED(); }

BufferHandle& BufferHandle::operator=(BufferHandle&&) noexcept { CPPCOLDB_NOT_IMPLEMENTED(); }

void BufferHandle::MarkDirty() { CPPCOLDB_NOT_IMPLEMENTED(); }

BufferManager::BufferManager(std::size_t pool_bytes, std::size_t block_size, IBlockManager* file)
    : pool_bytes_(pool_bytes), block_size_(block_size), file_(file) {}

std::unique_ptr<IBufferHandle> BufferManager::Pin(common::BlockId /*id*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void BufferManager::MarkDirty(common::BlockId /*id*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

std::unique_ptr<IBufferHandle> BufferManager::AllocateBlock() { CPPCOLDB_NOT_IMPLEMENTED(); }

void BufferManager::Flush() { CPPCOLDB_NOT_IMPLEMENTED(); }

void BufferManager::Shutdown() { CPPCOLDB_NOT_IMPLEMENTED(); }

void BufferManager::Unpin(common::BlockId /*id*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void BufferManager::EvictOne() { CPPCOLDB_NOT_IMPLEMENTED(); }

void BufferManager::EnsureSpace() { CPPCOLDB_NOT_IMPLEMENTED(); }

common::BlockId BufferManager::NextBlockId() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::storage
