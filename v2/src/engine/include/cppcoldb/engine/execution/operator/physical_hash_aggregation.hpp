#pragma once

#include <memory>
#include <vector>

#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/common/types/value.hpp"
#include "cppcoldb/engine/execution/aggregate_hash_table.hpp"
#include "cppcoldb/engine/execution/operator/expr_evaluator.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

struct HashAggState : public OperatorState {
    std::shared_ptr<AggregateHashTable> ht;
    std::vector<common::Value>          group_key;
};

// SINK: consumes input rows, evaluating `group_exprs` to find/create each
// row's AggEntry in `ht` and folding `aggr_exprs` into its AggregateStates.
class PhysicalHashAggregation : public PhysicalOperator {
public:
    PhysicalHashAggregation() { role = OperatorRole::SINK; }

    std::unique_ptr<OperatorState> CreateSinkState() const override;
    void Consume(const common::DataChunk& input, OperatorState& state,
                 ::cppcoldb::ClientContext& ctx) override;
    void Finalize(OperatorState& state, ::cppcoldb::ClientContext& ctx) override;

    std::vector<std::unique_ptr<Expression>>   group_exprs;
    std::vector<std::unique_ptr<Expression>>   aggr_exprs;
    std::vector<common::TypeId>                aggr_result_types;
    std::shared_ptr<AggregateHashTable>         ht;
    std::unique_ptr<PhysicalOperator>           source_op;
};

} // namespace cppcoldb::engine::execution
