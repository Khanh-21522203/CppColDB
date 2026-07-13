#pragma once
#include <compare>
#include <cstdint>
#include <functional>

namespace cppcoldb::common {

// Identifies a column by ordinal position within a table schema.
struct ColumnId {
    std::uint32_t value = 0;
    constexpr ColumnId() = default;
    constexpr explicit ColumnId(std::uint32_t v) : value(v) {}
    auto operator<=>(const ColumnId&) const = default;
};

} // namespace cppcoldb::common

template <>
struct std::hash<cppcoldb::common::ColumnId> {
    std::size_t operator()(const cppcoldb::common::ColumnId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value);
    }
};
