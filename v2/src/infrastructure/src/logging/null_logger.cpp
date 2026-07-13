#include "cppcoldb/infrastructure/logging/null_logger.hpp"

namespace cppcoldb::infrastructure::logging {

void NullLogger::Log(engine::io::LogLevel level, const std::string& message) {}

} // namespace cppcoldb::infrastructure::logging
