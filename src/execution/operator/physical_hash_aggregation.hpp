#pragma once
#include <memory>
#include <vector>
#include "execution/physical_operator.hpp"
#include "execution/pipeline.hpp"
#include "execution/aggregate_hash_table.hpp"

namespace cppcoldb {

struct HashAggState : OperatorState {
    std::shared_ptr<AggregateHashTable> ht;
};

// SINK: consumes input chunks, building the aggregate hash table.
// After Finalize(), the shared AggregateHashTable is ready to be read by
// PhysicalAggregationSource in the second (emit) pipeline.
struct PhysicalHashAggregation : PhysicalOperator {
    std::vector<std::unique_ptr<LogicalExpr>> group_exprs;
    std::vector<std::unique_ptr<LogicalExpr>> aggr_exprs;  // LogicalAggrExpr nodes
    std::vector<TypeId>                       aggr_result_types; // per aggr_expr

    // Shared hash table — also held by the paired PhysicalAggregationSource.
    std::shared_ptr<AggregateHashTable> ht;

    // SOURCE operator for the emit pipeline; owned here, referenced by Executor.
    std::unique_ptr<PhysicalOperator> source_op;

    PhysicalHashAggregation() { role = OperatorRole::SINK; }

    std::unique_ptr<OperatorState> CreateSinkState() const override;
    void Consume (const DataChunk& input, OperatorState& state, ClientContext& ctx) override;
    void Finalize(OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
