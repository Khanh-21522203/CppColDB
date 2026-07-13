#pragma once
#include "cppcoldb/common/types/timestamp.hpp"

namespace cppcoldb::engine::io {

// Port over wall-clock time, so the engine never calls system clock APIs
// directly (real clock in infrastructure, deterministic fake in tests).
class IClock {
public:
    virtual ~IClock() = default;

    // Current time, in microseconds since epoch.
    virtual common::Timestamp NowMicros() const = 0;
};

} // namespace cppcoldb::engine::io
