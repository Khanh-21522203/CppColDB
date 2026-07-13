#pragma once
#include <cstddef>
#include <cstdint>

namespace cppcoldb::common {

// Maximum rows processed per DataChunk (vectorized batch size).
inline constexpr std::size_t STANDARD_VECTOR_SIZE = 1024;

// Default RowGroup row capacity (120 * 1024).
inline constexpr std::uint32_t ROW_GROUP_SIZE = 122880;

// Default storage block size in bytes (256 KiB).
inline constexpr std::size_t DEFAULT_BLOCK_SIZE = 262144;

} // namespace cppcoldb::common
