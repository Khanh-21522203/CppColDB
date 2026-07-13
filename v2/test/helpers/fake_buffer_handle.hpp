#pragma once
#include <cstdint>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/storage/i_buffer_handle.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for IBufferHandle. Not executed; every method throws.
class FakeBufferHandle final : public engine::storage::IBufferHandle {
public:
    std::uint8_t*   Data()  const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    common::BlockId Id()    const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    bool            Valid() const override { CPPCOLDB_NOT_IMPLEMENTED(); }

    void MarkDirty() override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
