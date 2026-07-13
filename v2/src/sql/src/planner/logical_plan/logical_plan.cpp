#include "cppcoldb/sql/planner/logical_plan/logical_plan.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::sql::planner {

std::unique_ptr<LogicalExpr> CloneExpr(const LogicalExpr&) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::sql::planner
