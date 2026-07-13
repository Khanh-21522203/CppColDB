#pragma once

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/io/i_clock.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for IClock. Not executed; every method throws.
class FakeClock final : public engine::io::IClock {
public:
    common::Timestamp NowMicros() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
