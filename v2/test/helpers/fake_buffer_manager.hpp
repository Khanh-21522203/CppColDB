#pragma once
#include <cstddef>
#include <memory>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_handle.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_manager.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for IBufferManager. Not executed; every method throws.
class FakeBufferManager final : public engine::storage::IBufferManager {
public:
    std::unique_ptr<engine::storage::IBufferHandle> Pin(common::BlockId id) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void MarkDirty(common::BlockId id) override { CPPCOLDB_NOT_IMPLEMENTED(); }

    std::unique_ptr<engine::storage::IBufferHandle> AllocateBlock() override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void Flush() override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void Shutdown() override { CPPCOLDB_NOT_IMPLEMENTED(); }

    std::size_t BlockSize()  const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    bool        IsInMemory() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
