#pragma once
#include "execution/physical_operator.hpp"
#include "planner/logical_plan/logical_plan.hpp"
#include <memory>

namespace cppcoldb {

struct PhysicalDelete : PhysicalOperator {
    std::string schema_name;
    std::string table_name;

    // Optional filter predicate (may be null = delete all visible rows).
    std::unique_ptr<LogicalExpr> predicate;

    PhysicalDelete() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
