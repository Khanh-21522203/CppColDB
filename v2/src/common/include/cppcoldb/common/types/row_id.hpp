#pragma once
#include <compare>
#include <cstdint>
#include <functional>

#include "cppcoldb/common/types/row_group_id.hpp"

namespace cppcoldb::common {

// Logical row identifier: upper 32 bits = RowGroupId, lower 32 bits = row offset.
struct RowId {
    std::uint64_t value = 0;
    constexpr RowId() = default;
    constexpr explicit RowId(std::uint64_t v) : value(v) {}
    auto operator<=>(const RowId&) const = default;
};

inline constexpr RowId MakeRowId(RowGroupId rg, std::uint32_t offset) {
    return RowId{(static_cast<std::uint64_t>(rg.value) << 32) | offset};
}
inline constexpr RowGroupId RowIdGroup(RowId r) {
    return RowGroupId{static_cast<std::uint32_t>(r.value >> 32)};
}
inline constexpr std::uint32_t RowIdOffset(RowId r) {
    return static_cast<std::uint32_t>(r.value);
}

} // namespace cppcoldb::common

template <>
struct std::hash<cppcoldb::common::RowId> {
    std::size_t operator()(const cppcoldb::common::RowId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
