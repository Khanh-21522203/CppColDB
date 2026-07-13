#include "cppcoldb/sql/planner/logical_planner.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::sql::planner {

LogicalPlanner::LogicalPlanner(engine::catalog::ICatalog& catalog,
                                engine::transaction::ITransaction& tx)
    : binder_(catalog, tx) {}

std::unique_ptr<LogicalPlan> LogicalPlanner::Plan(const parser::ParsedStatement&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::sql::planner
