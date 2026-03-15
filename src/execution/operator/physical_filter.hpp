#pragma once
#include "execution/physical_operator.hpp"
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

struct PhysicalFilter : PhysicalOperator {
    std::unique_ptr<LogicalExpr> predicate;

    PhysicalFilter() { role = OperatorRole::OPERATOR; }

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
