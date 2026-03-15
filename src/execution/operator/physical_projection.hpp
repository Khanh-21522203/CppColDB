#pragma once
#include "execution/physical_operator.hpp"
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

struct PhysicalProjection : PhysicalOperator {
    std::vector<std::unique_ptr<LogicalExpr>> exprs;

    PhysicalProjection() { role = OperatorRole::OPERATOR; }

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
