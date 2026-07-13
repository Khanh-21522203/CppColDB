#pragma once
#include <cstddef>

namespace cppcoldb::common {

// Common cache-line size across x86-64 / ARM64. Kept a fixed constant (rather than
// std::hardware_destructive_interference_size) to avoid ABI-instability warnings.
inline constexpr std::size_t CACHE_LINE_SIZE = 64;

// Pads a value to its own cache line to avoid false sharing between threads.
template <typename T>
struct alignas(CACHE_LINE_SIZE) CacheLinePadded {
    T value{};
    CacheLinePadded() = default;
    explicit CacheLinePadded(T v) : value(std::move(v)) {}
    T& operator*() { return value; }
    const T& operator*() const { return value; }
};

} // namespace cppcoldb::common
