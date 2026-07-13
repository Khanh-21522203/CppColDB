#pragma once
#include <cstddef>
#include <optional>

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::common {

// Thread-safe multi-producer / multi-consumer queue used by the task scheduler.
// Scaffold: interface fixed, bodies unimplemented.
template <typename T>
class ConcurrentQueue {
public:
    void Push(T /*item*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

    // Non-blocking pop; std::nullopt when empty.
    std::optional<T> TryPop() { CPPCOLDB_NOT_IMPLEMENTED(); }

    bool Empty() const { CPPCOLDB_NOT_IMPLEMENTED(); }
    std::size_t Size() const { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::common
