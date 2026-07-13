#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/common/types/value.hpp"
#include "cppcoldb/engine/execution/join_hash_table.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"
#include "cppcoldb/engine/storage/partition_info.hpp"

namespace cppcoldb::engine::execution {

// SINK: consumes the build (right) side of a hash join, inserting every row
// into `ht` (or, for partitioned joins, the matching entry of
// `partition_hts`) keyed by `key_col_idxs`.
class PhysicalHashJoinBuild : public PhysicalOperator {
public:
    PhysicalHashJoinBuild() { role = OperatorRole::SINK; }

    void Consume(const common::DataChunk& input, OperatorState& state,
                 ::cppcoldb::ClientContext& ctx) override;
    void Finalize(OperatorState& state, ::cppcoldb::ClientContext& ctx) override {}

    std::shared_ptr<JoinHashTable>              ht;
    std::vector<common::ColumnId>               key_col_idxs;
    std::vector<std::shared_ptr<JoinHashTable>> partition_hts;
    storage::PartitionInfo                      right_partition_info;
};

// Cursor over the current probe row's matches within the build-side hash
// table: which match (if any) is being emitted next.
struct JoinProbeState : public OperatorState {
    std::size_t                 probe_row_idx = 0;
    std::size_t                 match_idx = 0;
    std::vector<common::Value>  probe_key;
};

// OPERATOR: probes the build side's hash table with each input (left/probe
// side) row's key, emitting one output row per match.
class PhysicalHashJoinProbe : public PhysicalOperator {
public:
    PhysicalHashJoinProbe() { role = OperatorRole::OPERATOR; }

    std::unique_ptr<OperatorState> CreateOperatorState() const override {
        return std::make_unique<JoinProbeState>();
    }
    OperatorResultType Execute(const common::DataChunk& input, common::DataChunk& output,
                                OperatorState& state, ::cppcoldb::ClientContext& ctx) override;

    std::shared_ptr<JoinHashTable>              ht;
    std::vector<common::ColumnId>               left_key_col_idxs;
    std::size_t                                 left_col_count = 0;
    std::vector<std::shared_ptr<JoinHashTable>> partition_hts;
    storage::PartitionInfo                      left_partition_info;
    std::unique_ptr<PhysicalOperator>           build_op;
};

} // namespace cppcoldb::engine::execution
