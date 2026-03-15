#pragma once
#include "execution/physical_operator.hpp"
#include "execution/pipeline.hpp"
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

struct TableScanState : OperatorState {
    size_t row_group_idx       = 0;
    size_t row_offset_in_group = 0;
};

struct PhysicalTableScan : PhysicalOperator {
    std::string schema_name;
    std::string table_name;
    std::vector<size_t> column_ids;
    std::vector<std::unique_ptr<LogicalExpr>> pushed_filters;

    PhysicalTableScan() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
