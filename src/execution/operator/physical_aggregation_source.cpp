#include "execution/operator/physical_aggregation_source.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalAggregationSource::CreateScanState() const {
    return std::make_unique<AggSourceState>();
}

OperatorResultType PhysicalAggregationSource::GetData(OperatorState& state_base,
                                                       DataChunk& output,
                                                       ClientContext& /*ctx*/) {
    auto& state = static_cast<AggSourceState&>(state_base);

    output.Initialize(output_types);

    size_t n    = ht->EntryCount();
    size_t emit = 0;

    // Emit an empty row for scalar aggregates on an empty table (0 groups, but
    // COUNT(*) should return 0, not an empty result set).
    // Handled by FindOrCreate always creating a group even for empty input?
    // Actually: if the table is empty, Consume is never called, so the HT has 0 groups.
    // For scalar aggregates on empty input the standard behavior is to return one row
    // with the initial aggregate values (0 for COUNT, NULL for SUM/MIN/MAX/AVG).
    // We model this by emitting one synthetic entry when num_group_cols == 0 and n == 0.
    bool synthetic_empty_agg = (num_group_cols == 0 && n == 0 && state.entry_idx == 0);

    if (synthetic_empty_agg) {
        // Emit one row of zero/null aggregate results.
        for (size_t j = 0; j < agg_funcs.size(); ++j) {
            AggregateState empty_state;
            Value v = AggFinalize(empty_state, agg_funcs[j], agg_result_types[j]);
            DataVectorAppendValue(output.columns[j], v);
        }
        output.count = 1;
        state.entry_idx = 1; // mark as done
        return OperatorResultType::FINISHED;
    }

    while (state.entry_idx < n && emit < STANDARD_VECTOR_SIZE) {
        const AggEntry& entry = ht->GetEntry(state.entry_idx++);

        // Emit group key columns first.
        for (size_t k = 0; k < num_group_cols; ++k) {
            DataVectorAppendValue(output.columns[k], entry.group_key[k]);
        }

        // Emit finalized aggregate results.
        for (size_t j = 0; j < agg_funcs.size(); ++j) {
            Value v = AggFinalize(entry.agg_states[j], agg_funcs[j], agg_result_types[j]);
            DataVectorAppendValue(output.columns[num_group_cols + j], v);
        }

        ++emit;
    }

    output.count = emit;

    if (state.entry_idx >= n) return OperatorResultType::FINISHED;
    return OperatorResultType::HAVE_MORE_OUTPUT;
}

} // namespace cppcoldb
