#include "cppcoldb/sql/planner/bind_context.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::sql::planner {

void BindContext::AddTable(std::size_t, const std::string&,
                            const std::vector<std::string>&,
                            const std::vector<common::TypeId>&,
                            std::size_t) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

ColumnBinding BindContext::ResolveColumn(const std::string&) const { CPPCOLDB_NOT_IMPLEMENTED(); }

ColumnBinding BindContext::ResolveQualified(const std::string&, const std::string&) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::sql::planner
