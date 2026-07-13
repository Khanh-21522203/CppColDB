#include "cppcoldb/client_context.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb {

ClientContext::ClientContext() {}

ClientContext::~ClientContext() {}

QueryResult ClientContext::Query(const std::string& sql) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb
