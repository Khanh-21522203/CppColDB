#pragma once
#include <memory>

#include "cppcoldb/sql/planner/binder.hpp"
#include "cppcoldb/sql/planner/logical_plan/logical_plan.hpp"

namespace cppcoldb::sql::parser {
struct ParsedStatement;
} // namespace cppcoldb::sql::parser

namespace cppcoldb::sql::planner {

// Top-level entry point: parsed statement -> bound LogicalPlan.
class LogicalPlanner {
public:
    LogicalPlanner(engine::catalog::ICatalog& catalog, engine::transaction::ITransaction& tx);
    std::unique_ptr<LogicalPlan> Plan(const parser::ParsedStatement& stmt);

private:
    Binder binder_;
};

} // namespace cppcoldb::sql::planner
