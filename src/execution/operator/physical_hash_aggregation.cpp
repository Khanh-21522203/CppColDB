#include "execution/operator/physical_hash_aggregation.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalHashAggregation::CreateSinkState() const {
    auto s = std::make_unique<HashAggState>();
    s->ht  = ht;  // share the pre-allocated hash table
    return s;
}

void PhysicalHashAggregation::Consume(const DataChunk& input,
                                       OperatorState& state_base,
                                       ClientContext& /*ctx*/) {
    auto& state = static_cast<HashAggState&>(state_base);
    if (input.count == 0) return;

    // Pre-evaluate all group-by expressions on the full chunk.
    std::vector<DataVector> group_vecs(group_exprs.size());
    for (size_t k = 0; k < group_exprs.size(); ++k) {
        ExprEvaluator::Evaluate(*group_exprs[k], input, group_vecs[k]);
    }

    // Pre-evaluate aggregate argument expressions (skip for COUNT(*) which is_star).
    std::vector<DataVector> agg_vecs(aggr_exprs.size());
    for (size_t j = 0; j < aggr_exprs.size(); ++j) {
        const auto& agg = static_cast<const LogicalAggrExpr&>(*aggr_exprs[j]);
        if (!agg.is_star) {
            ExprEvaluator::Evaluate(*agg.arg, input, agg_vecs[j]);
        }
    }

    // Process each row.
    for (size_t row = 0; row < input.count; ++row) {
        // Build group key.
        std::vector<Value> key;
        key.reserve(group_exprs.size());
        for (size_t k = 0; k < group_exprs.size(); ++k) {
            key.push_back(VectorGetValue(group_vecs[k], row));
        }

        AggEntry* entry = state.ht->FindOrCreate(key);

        // Update each aggregate state.
        for (size_t j = 0; j < aggr_exprs.size(); ++j) {
            const auto& agg = static_cast<const LogicalAggrExpr&>(*aggr_exprs[j]);
            if (agg.is_star) {
                ++entry->agg_states[j].count;
            } else {
                AggUpdate(entry->agg_states[j], agg.func, agg_vecs[j], row);
            }
        }
    }
}

void PhysicalHashAggregation::Finalize(OperatorState& /*state*/, ClientContext& /*ctx*/) {
    // Hash table is now complete; the emit pipeline will read it via source_op.
}

} // namespace cppcoldb
