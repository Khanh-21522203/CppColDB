#pragma once
#include "execution/physical_operator.hpp"
#include "planner/logical_plan/logical_plan.hpp"
#include <memory>
#include <vector>

namespace cppcoldb {

struct PhysicalUpdate : PhysicalOperator {
    std::string schema_name;
    std::string table_name;

    // Columns to update (indices in the table's column list).
    std::vector<size_t> update_col_ids;

    // Expressions that produce new values (parallel to update_col_ids).
    // Each expression is evaluated against the full row of the table.
    std::vector<std::unique_ptr<LogicalExpr>> update_exprs;

    // Optional WHERE predicate (null = update all visible rows).
    std::unique_ptr<LogicalExpr> predicate;

    PhysicalUpdate() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
