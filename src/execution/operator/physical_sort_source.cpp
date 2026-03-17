#include "execution/operator/physical_sort_source.hpp"
#include "common/types.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalSortSource::CreateScanState() const {
    return std::make_unique<SortSourceState>();
}

OperatorResultType PhysicalSortSource::GetData(OperatorState& raw_state,
                                                DataChunk& output,
                                                ClientContext& /*ctx*/) {
    auto& state = static_cast<SortSourceState&>(raw_state);

    if (!buf->sorted) return OperatorResultType::FINISHED;

    const auto& order = buf->sorted_order;
    if (state.chunk_idx >= order.size()) return OperatorResultType::FINISHED;

    // Build output column types from the first available chunk.
    if (buf->chunks.empty()) return OperatorResultType::FINISHED;
    std::vector<TypeId> types;
    for (const auto& col : buf->chunks[0].columns) types.push_back(col.type);
    output.Initialize(types);

    size_t emitted = 0;
    while (state.chunk_idx < order.size() && emitted < STANDARD_VECTOR_SIZE) {
        auto [ci, ri] = order[state.chunk_idx];
        const DataChunk& src = buf->chunks[ci];
        for (size_t col = 0; col < src.ColumnCount(); ++col) {
            DataVectorAppend(output.columns[col], src.columns[col], ri);
        }
        ++emitted;
        ++state.chunk_idx;
    }
    output.count = emitted;

    if (state.chunk_idx >= order.size()) return OperatorResultType::FINISHED;
    return OperatorResultType::HAVE_MORE_OUTPUT;
}

} // namespace cppcoldb
