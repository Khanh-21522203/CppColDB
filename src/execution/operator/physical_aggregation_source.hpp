#pragma once
#include <memory>
#include "execution/physical_operator.hpp"
#include "execution/pipeline.hpp"
#include "execution/aggregate_hash_table.hpp"

namespace cppcoldb {

struct AggSourceState : OperatorState {
    size_t entry_idx = 0;
};

// SOURCE: iterates the AggregateHashTable and emits finalized result chunks.
// Runs in the second (emit) pipeline after the consume pipeline completes.
struct PhysicalAggregationSource : PhysicalOperator {
    std::shared_ptr<AggregateHashTable> ht;

    // Parallel to group_exprs in PhysicalHashAggregation — types for group cols.
    size_t num_group_cols = 0;

    // AggFunc and result_type per aggregate column (positions after group cols).
    std::vector<LogicalAggrExpr::AggFunc> agg_funcs;
    std::vector<TypeId>                   agg_result_types;

    PhysicalAggregationSource() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override {}
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
