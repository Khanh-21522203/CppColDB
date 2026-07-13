#pragma once
#include "cppcoldb/engine/abstractions/io/i_logger.hpp"

namespace cppcoldb::infrastructure::logging {

// Adapter that writes log messages to stdout/stderr.
class ConsoleLogger final : public engine::io::ILogger {
public:
    ConsoleLogger() = default;
    ~ConsoleLogger() override = default;

    void Log(engine::io::LogLevel level, const std::string& message) override;
};

} // namespace cppcoldb::infrastructure::logging
