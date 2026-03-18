#pragma once
#include "execution/physical_operator.hpp"
#include "execution/join_hash_table.hpp"
#include "execution/pipeline.hpp"
#include <vector>
#include <memory>

namespace cppcoldb {

// Build side (SINK): consumes right-side scan rows and builds the hash table.
struct PhysicalHashJoinBuild : PhysicalOperator {
    PhysicalHashJoinBuild() { role = OperatorRole::SINK; }

    std::shared_ptr<JoinHashTable> ht;
    std::vector<size_t>            key_col_idxs; // key positions within right-side chunk

    void Consume(const DataChunk& input, OperatorState& state,
                 ClientContext& ctx) override;
    void Finalize(OperatorState& state, ClientContext& ctx) override {}
};

// Per-probe-call state: tracks resume position within the current input batch.
struct JoinProbeState : OperatorState {
    size_t probe_row_idx = 0; // next input row to process
    size_t match_idx     = 0; // next match to emit for the current input row
    // Reused across rows to avoid 100K per-row heap allocations.
    std::vector<Value> probe_key;
};

// Probe side (OPERATOR): for each left-side row, probes the hash table and
// emits concatenated (left || right) output rows.
struct PhysicalHashJoinProbe : PhysicalOperator {
    PhysicalHashJoinProbe() { role = OperatorRole::OPERATOR; }

    std::shared_ptr<JoinHashTable>    ht;
    std::vector<size_t>               left_key_col_idxs; // key positions in left chunk
    size_t                            left_col_count = 0;

    // Owns the build-side pipeline root so Executor::BuildPipelines can reach it.
    std::unique_ptr<PhysicalOperator> build_op;

    std::unique_ptr<OperatorState> CreateOperatorState() const override {
        return std::make_unique<JoinProbeState>();
    }

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                               OperatorState& state, ClientContext& ctx) override;
};

} // namespace cppcoldb
