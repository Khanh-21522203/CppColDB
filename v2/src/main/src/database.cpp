#include "cppcoldb/database.hpp"

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/connection.hpp"

namespace cppcoldb {

Database::Database(const std::string& path, DatabaseConfig config)
    : path_(path), config_(std::move(config)) {}

Database::~Database() {}

std::unique_ptr<Connection> Database::Connect() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb
