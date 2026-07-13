#pragma once
#include "cppcoldb/engine/abstractions/io/i_clock.hpp"

namespace cppcoldb::infrastructure::time {

// Real wall-clock adapter for IClock.
class SystemClock final : public engine::io::IClock {
public:
    SystemClock() = default;
    ~SystemClock() override = default;

    common::Timestamp NowMicros() const override;
};

} // namespace cppcoldb::infrastructure::time
