#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/execution/aggregate_hash_table.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"

namespace cppcoldb::engine::execution {

struct AggSourceState : public OperatorState {
    std::size_t entry_idx = 0;
};

// SOURCE: streams one output row per AggEntry in `ht`, finalizing each
// aggregate expression's AggregateState via `agg_funcs`/`agg_result_types`.
class PhysicalAggregationSource : public PhysicalOperator {
public:
    PhysicalAggregationSource() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) override {}
    OperatorResultType GetData(OperatorState& state, common::DataChunk& output,
                                ::cppcoldb::ClientContext& ctx) override;

    std::shared_ptr<AggregateHashTable> ht;
    std::size_t                         num_group_cols = 0;
    std::vector<AggFunc>                agg_funcs;
    std::vector<common::TypeId>         agg_result_types;
};

} // namespace cppcoldb::engine::execution
