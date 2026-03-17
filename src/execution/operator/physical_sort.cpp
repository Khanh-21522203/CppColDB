#include "execution/operator/physical_sort.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "execution/aggregate_hash_table.hpp"  // VectorGetValue
#include "common/exception.hpp"

#include <algorithm>
#include <numeric>

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalSort::CreateSinkState() const {
    auto s   = std::make_unique<SortSinkState>();
    s->buf   = buf;
    return s;
}

void PhysicalSort::Consume(const DataChunk& input,
                            OperatorState& /*state*/,
                            ClientContext& /*ctx*/) {
    if (input.count == 0) return;

    // Deep-copy the input chunk into the buffer (input may be reused by the pipeline).
    DataChunk copy;
    std::vector<TypeId> types;
    types.reserve(input.ColumnCount());
    for (const auto& col : input.columns) types.push_back(col.type);
    copy.Initialize(types);
    std::vector<uint32_t> all_rows(input.count);
    std::iota(all_rows.begin(), all_rows.end(), 0);
    DataChunkSlice(copy, input, all_rows);
    buf->chunks.push_back(std::move(copy));
}

void PhysicalSort::Finalize(OperatorState& /*state*/, ClientContext& /*ctx*/) {
    // Evaluate sort key expressions for every input chunk.
    buf->key_vecs.resize(sort_keys.size());
    for (size_t ki = 0; ki < sort_keys.size(); ++ki) {
        buf->key_vecs[ki].resize(buf->chunks.size());
        for (size_t ci = 0; ci < buf->chunks.size(); ++ci) {
            buf->key_vecs[ki][ci].Reset(TypeId::INVALID, 0);
            ExprEvaluator::Evaluate(*sort_keys[ki], buf->chunks[ci],
                                    buf->key_vecs[ki][ci]);
        }
    }

    // Build flat index of all rows.
    size_t total = 0;
    for (const auto& c : buf->chunks) total += c.count;
    buf->sorted_order.reserve(total);
    for (size_t ci = 0; ci < buf->chunks.size(); ++ci) {
        for (size_t ri = 0; ri < buf->chunks[ci].count; ++ri) {
            buf->sorted_order.emplace_back(ci, ri);
        }
    }

    // Sort by sort keys.
    const auto& keys     = buf->key_vecs;
    const auto& asc      = ascending;
    std::stable_sort(buf->sorted_order.begin(), buf->sorted_order.end(),
        [&](const std::pair<size_t,size_t>& a, const std::pair<size_t,size_t>& b) {
            for (size_t ki = 0; ki < keys.size(); ++ki) {
                Value va = VectorGetValue(keys[ki][a.first], a.second);
                Value vb = VectorGetValue(keys[ki][b.first], b.second);
                // Deterministic NULL handling: NULLS LAST for both ASC/DESC.
                if (va.IsNull() || vb.IsNull()) {
                    if (va.IsNull() && vb.IsNull()) continue;
                    if (va.IsNull()) return false;
                    return true;
                }
                if (va < vb) return  asc[ki];
                if (vb < va) return !asc[ki];
            }
            return false; // equal
        });

    buf->sorted = true;
}

} // namespace cppcoldb
