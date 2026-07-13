#pragma once
#include <cstdint>

#include "cppcoldb/common/types/block_id.hpp"

namespace cppcoldb::engine::storage {

// RAII handle over a pinned buffer pool frame. Implementations unpin the
// underlying block when destroyed.
class IBufferHandle {
public:
    virtual ~IBufferHandle() = default;

    virtual std::uint8_t*   Data()  const = 0;
    virtual common::BlockId Id()    const = 0;
    virtual bool            Valid() const = 0;

    // Marks the pinned block dirty via the owning buffer manager.
    virtual void MarkDirty() = 0;
};

} // namespace cppcoldb::engine::storage
