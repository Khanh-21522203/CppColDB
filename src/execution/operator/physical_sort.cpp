#include "execution/operator/physical_sort.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "execution/aggregate_hash_table.hpp"  // VectorGetValue
#include "common/exception.hpp"

#include <algorithm>
#include <numeric>

namespace cppcoldb {

namespace {

int CompareSortRows(const std::vector<std::vector<DataVector>>& keys,
                    const std::vector<bool>& asc,
                    const std::pair<size_t, size_t>& a,
                    const std::pair<size_t, size_t>& b) {
    for (size_t ki = 0; ki < keys.size(); ++ki) {
        Value va = VectorGetValue(keys[ki][a.first], a.second);
        Value vb = VectorGetValue(keys[ki][b.first], b.second);

        // Deterministic NULL handling: NULLS LAST for both ASC/DESC.
        if (va.IsNull() || vb.IsNull()) {
            if (va.IsNull() && vb.IsNull()) {
                continue;
            }
            if (va.IsNull()) return 1;
            return -1;
        }

        if (va < vb) return asc[ki] ? -1 : 1;
        if (vb < va) return asc[ki] ? 1 : -1;
    }

    // Stable tie-break by original row order.
    if (a.first != b.first) return (a.first < b.first) ? -1 : 1;
    if (a.second != b.second) return (a.second < b.second) ? -1 : 1;
    return 0;
}

} // namespace

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

    // Sort by sort keys (or top-k if requested).
    const auto& keys     = buf->key_vecs;
    const auto& asc      = ascending;
    auto less_row = [&](const std::pair<size_t, size_t>& a,
                        const std::pair<size_t, size_t>& b) {
        return CompareSortRows(keys, asc, a, b) < 0;
    };

    if (top_k == 0) {
        buf->sorted_order.clear();
    } else if (top_k > 0) {
        const size_t k = static_cast<size_t>(top_k);
        std::vector<std::pair<size_t, size_t>> heap;
        heap.reserve(k);

        for (size_t ci = 0; ci < buf->chunks.size(); ++ci) {
            for (size_t ri = 0; ri < buf->chunks[ci].count; ++ri) {
                std::pair<size_t, size_t> row{ci, ri};
                if (heap.size() < k) {
                    heap.push_back(row);
                    std::push_heap(heap.begin(), heap.end(), less_row);
                    continue;
                }
                // heap.front() is the worst currently kept row.
                if (less_row(row, heap.front())) {
                    std::pop_heap(heap.begin(), heap.end(), less_row);
                    heap.back() = row;
                    std::push_heap(heap.begin(), heap.end(), less_row);
                }
            }
        }

        std::sort(heap.begin(), heap.end(), less_row);
        buf->sorted_order = std::move(heap);
    } else {
        // Build flat index of all rows.
        size_t total = 0;
        for (const auto& c : buf->chunks) total += c.count;
        buf->sorted_order.clear();
        buf->sorted_order.reserve(total);
        for (size_t ci = 0; ci < buf->chunks.size(); ++ci) {
            for (size_t ri = 0; ri < buf->chunks[ci].count; ++ri) {
                buf->sorted_order.emplace_back(ci, ri);
            }
        }
        std::stable_sort(buf->sorted_order.begin(), buf->sorted_order.end(), less_row);
    }

    buf->sorted = true;
}

} // namespace cppcoldb
