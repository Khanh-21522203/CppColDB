#pragma once
#include <compare>
#include <cstdint>
#include <functional>

namespace cppcoldb::common {

// Identifies a RowGroup within a table (index in the RowGroup list).
struct RowGroupId {
    std::uint32_t value = 0;
    constexpr RowGroupId() = default;
    constexpr explicit RowGroupId(std::uint32_t v) : value(v) {}
    auto operator<=>(const RowGroupId&) const = default;
};

} // namespace cppcoldb::common

template <>
struct std::hash<cppcoldb::common::RowGroupId> {
    std::size_t operator()(const cppcoldb::common::RowGroupId& id) const noexcept {
        return std::hash<std::uint32_t>{}(id.value);
    }
};
