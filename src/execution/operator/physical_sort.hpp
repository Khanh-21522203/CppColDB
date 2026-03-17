#pragma once
#include <memory>
#include <vector>
#include "execution/physical_operator.hpp"
#include "execution/pipeline.hpp"
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

// Shared between SINK (PhysicalSort) and SOURCE (PhysicalSortSource).
struct SortBuffer {
    std::vector<DataChunk> chunks;         // input rows (un-reordered)
    // key_vecs[sort_key_idx][chunk_idx] = evaluated key column for that chunk
    std::vector<std::vector<DataVector>> key_vecs;
    // Sorted flat index: (chunk_idx, row_in_chunk) in sort order
    std::vector<std::pair<size_t, size_t>> sorted_order;
    bool sorted = false;
};

struct SortSinkState : OperatorState {
    std::shared_ptr<SortBuffer> buf;
};

// SINK: accumulates all input chunks into SortBuffer.
// Finalize() evaluates sort keys and computes sorted_order.
struct PhysicalSort : PhysicalOperator {
    std::vector<std::unique_ptr<LogicalExpr>> sort_keys;
    std::vector<bool>                         ascending; // parallel to sort_keys

    // Shared buffer — also held by PhysicalSortSource.
    std::shared_ptr<SortBuffer> buf;

    // SOURCE operator for the emit pipeline; owned here, referenced by Executor.
    std::unique_ptr<PhysicalOperator> source_op;

    PhysicalSort() { role = OperatorRole::SINK; }

    std::unique_ptr<OperatorState> CreateSinkState() const override;
    void Consume (const DataChunk& input, OperatorState& state, ClientContext& ctx) override;
    void Finalize(OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
