#pragma once
#include <cstddef>
#include <memory>

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::common {

// Recycles fixed-type objects to cut allocation churn on hot paths.
// Scaffold: interface fixed, bodies unimplemented.
template <typename T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t /*initial_capacity*/ = 0) {}

    // Borrow an object from the pool (allocating if empty).
    std::unique_ptr<T> Acquire() { CPPCOLDB_NOT_IMPLEMENTED(); }

    // Return an object to the pool for reuse.
    void Release(std::unique_ptr<T> /*obj*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

    std::size_t Size() const { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::common
