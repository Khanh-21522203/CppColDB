#pragma once
#include <compare>
#include <cstdint>
#include <functional>

namespace cppcoldb::common {

// Opaque identifier for a storage block managed by the buffer manager and block file.
struct BlockId {
    std::uint64_t value = 0;
    constexpr BlockId() = default;
    constexpr explicit BlockId(std::uint64_t v) : value(v) {}
    auto operator<=>(const BlockId&) const = default;
};

inline constexpr BlockId INVALID_BLOCK{UINT64_MAX};

} // namespace cppcoldb::common

template <>
struct std::hash<cppcoldb::common::BlockId> {
    std::size_t operator()(const cppcoldb::common::BlockId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
