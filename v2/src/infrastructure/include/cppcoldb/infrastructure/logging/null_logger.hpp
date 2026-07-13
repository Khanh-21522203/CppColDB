#pragma once
#include "cppcoldb/engine/abstractions/io/i_logger.hpp"

namespace cppcoldb::infrastructure::logging {

// No-op adapter: legitimately does nothing, so its override is trivially
// empty rather than throwing CPPCOLDB_NOT_IMPLEMENTED().
class NullLogger final : public engine::io::ILogger {
public:
    NullLogger() = default;
    ~NullLogger() override = default;

    void Log(engine::io::LogLevel level, const std::string& message) override;
};

} // namespace cppcoldb::infrastructure::logging
