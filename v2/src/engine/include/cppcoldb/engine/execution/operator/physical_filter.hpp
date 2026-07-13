#pragma once

#include <memory>

#include "cppcoldb/engine/execution/operator/expr_evaluator.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

// OPERATOR: evaluates `predicate` over the input chunk and passes through
// only the rows that satisfy it.
class PhysicalFilter : public PhysicalOperator {
public:
    PhysicalFilter() { role = OperatorRole::OPERATOR; }

    OperatorResultType Execute(const common::DataChunk& input, common::DataChunk& output,
                                OperatorState& state, ::cppcoldb::ClientContext& ctx) override;

    std::unique_ptr<Expression> predicate;
};

} // namespace cppcoldb::engine::execution
