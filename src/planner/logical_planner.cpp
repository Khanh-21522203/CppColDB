#include "planner/logical_planner.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
#include "parser/ast/parsed_statement.hpp"

namespace cppcoldb {

LogicalPlanner::LogicalPlanner(Catalog& catalog, Transaction& tx)
    : binder_(catalog, tx) {}

std::unique_ptr<LogicalPlan> LogicalPlanner::Plan(const ParsedStatement& stmt) {
    return binder_.Bind(stmt);
}

} // namespace cppcoldb
