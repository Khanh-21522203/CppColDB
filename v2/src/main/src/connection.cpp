#include "cppcoldb/connection.hpp"

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/database.hpp"

namespace cppcoldb {

Connection::Connection(Database& db) : db_(db) {}

Connection::~Connection() {}

QueryResult Connection::Query(const std::string& sql) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Connection::Begin() { CPPCOLDB_NOT_IMPLEMENTED(); }

void Connection::Commit() { CPPCOLDB_NOT_IMPLEMENTED(); }

void Connection::Rollback() { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb
