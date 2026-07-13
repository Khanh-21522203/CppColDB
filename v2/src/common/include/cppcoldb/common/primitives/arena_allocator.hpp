#pragma once
#include <cstddef>

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::common {

// Bump-pointer arena for short-lived, batch-freed allocations (e.g. per-query state).
// Scaffold: interface fixed, bodies unimplemented.
class ArenaAllocator {
public:
    explicit ArenaAllocator(std::size_t /*block_size*/ = 4096) {}

    // Allocate `bytes` with the given alignment from the current block.
    void* Allocate(std::size_t /*bytes*/, std::size_t /*alignment*/ = alignof(std::max_align_t)) {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    // Release all allocations at once (does not return memory to the OS).
    void Reset() { CPPCOLDB_NOT_IMPLEMENTED(); }

    std::size_t BytesAllocated() const { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::common
