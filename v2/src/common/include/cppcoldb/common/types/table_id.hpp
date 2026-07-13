#pragma once
#include <compare>
#include <cstdint>
#include <functional>

namespace cppcoldb::common {

// Catalog-assigned identifier for a table.
struct TableId {
    std::uint64_t value = 0;
    constexpr TableId() = default;
    constexpr explicit TableId(std::uint64_t v) : value(v) {}
    auto operator<=>(const TableId&) const = default;
};

inline constexpr TableId INVALID_TABLE{0};

} // namespace cppcoldb::common

template <>
struct std::hash<cppcoldb::common::TableId> {
    std::size_t operator()(const cppcoldb::common::TableId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
