#pragma once

#include <memory>
#include <vector>

#include "cppcoldb/engine/execution/operator/expr_evaluator.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// OPERATOR: evaluates `exprs` over the input chunk, producing one output
// column per expression.
class PhysicalProjection : public PhysicalOperator {
public:
    PhysicalProjection() { role = OperatorRole::OPERATOR; }

    OperatorResultType Execute(const common::DataChunk& input, common::DataChunk& output,
                                OperatorState& state, ::cppcoldb::ClientContext& ctx) override;

    std::vector<std::unique_ptr<Expression>> exprs;
};

} // namespace cppcoldb::engine::execution
