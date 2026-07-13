#pragma once

#include <memory>
#include <string>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/engine/execution/operator/expr_evaluator.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// SOURCE: a single GetData() call scans the table, evaluates `predicate` per
// row, and writes `update_exprs` into `update_col_ids` for every matching
// row; run-once via the generic ScanState cursor.
class PhysicalUpdate : public PhysicalOperator {
public:
    PhysicalUpdate() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;

    std::string                              schema_name;
    std::string                              table_name;
    std::vector<common::ColumnId>            update_col_ids;
    std::vector<common::ColumnId>            scan_col_ids;
    std::vector<std::unique_ptr<Expression>> update_exprs;
    std::unique_ptr<Expression>               predicate;
};

} // namespace cppcoldb::engine::execution
