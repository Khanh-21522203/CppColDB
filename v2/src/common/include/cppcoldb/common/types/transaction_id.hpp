#pragma once
#include <compare>
#include <cstdint>
#include <functional>

namespace cppcoldb::common {

// Monotonically assigned transaction identifier.
struct TransactionId {
    std::uint64_t value = 0;
    constexpr TransactionId() = default;
    constexpr explicit TransactionId(std::uint64_t v) : value(v) {}
    auto operator<=>(const TransactionId&) const = default;
};

inline constexpr TransactionId INVALID_TRANSACTION{0};

} // namespace cppcoldb::common

template <>
struct std::hash<cppcoldb::common::TransactionId> {
    std::size_t operator()(const cppcoldb::common::TransactionId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
