#pragma once
#include <string>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/io/i_logger.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for ILogger. Not executed; every method throws.
class FakeLogger final : public engine::io::ILogger {
public:
    void Log(engine::io::LogLevel level, const std::string& message) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
};

} // namespace cppcoldb::test
