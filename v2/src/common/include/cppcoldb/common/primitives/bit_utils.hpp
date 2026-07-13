#pragma once
#include <bit>
#include <cstdint>

namespace cppcoldb::common {

// Number of bits required to represent values in [0, max_value].
inline constexpr std::uint32_t BitsRequired(std::uint64_t max_value) {
    return max_value == 0 ? 0u : static_cast<std::uint32_t>(std::bit_width(max_value));
}

// Round up to the next power of two (used for hash-table capacity sizing).
inline constexpr std::uint64_t NextPowerOfTwo(std::uint64_t v) {
    return v <= 1 ? 1u : std::bit_ceil(v);
}

// Align size up to the given power-of-two alignment.
inline constexpr std::size_t AlignUp(std::size_t size, std::size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

} // namespace cppcoldb::common
