#pragma once
#include <compare>
#include <cstdint>
#include <functional>

namespace cppcoldb::common {

// Microseconds since epoch; used for MVCC snapshot / commit timestamps.
struct Timestamp {
    std::int64_t value = -1;
    constexpr Timestamp() = default;
    constexpr explicit Timestamp(std::int64_t v) : value(v) {}
    auto operator<=>(const Timestamp&) const = default;
};

inline constexpr Timestamp INVALID_TIMESTAMP{-1};

} // namespace cppcoldb::common

template <>
struct std::hash<cppcoldb::common::Timestamp> {
    std::size_t operator()(const cppcoldb::common::Timestamp& t) const noexcept {
        return std::hash<std::int64_t>{}(t.value);
    }
};
