#pragma once
#include <string>

namespace cppcoldb::engine::io {

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
};

// Port over diagnostic logging, so the engine never writes to stdout/files
// directly (console/null adapters live in infrastructure).
class ILogger {
public:
    virtual ~ILogger() = default;

    virtual void Log(LogLevel level, const std::string& message) = 0;
};

} // namespace cppcoldb::engine::io
