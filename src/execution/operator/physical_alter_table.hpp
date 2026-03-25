#pragma once
#include "execution/physical_operator.hpp"
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

struct PhysicalAlterTable : PhysicalOperator {
    std::string schema_name;
    std::string table_name;
    LogicalAlterTable::AlterKind kind;
    uint32_t    partition_idx = 0; // DROP
    std::vector<Value>              new_bound;  // ADD RANGE: one Value per key col
    std::vector<std::vector<Value>> new_values; // ADD LIST: outer=tuples, inner=key cols

    PhysicalAlterTable() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
