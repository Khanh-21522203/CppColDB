#include "execution/operator/physical_limit.hpp"
#include "common/types.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalLimit::CreateOperatorState() const {
    return std::make_unique<LimitState>();
}

OperatorResultType PhysicalLimit::Execute(const DataChunk& input, DataChunk& output,
                                           OperatorState& raw_state, ClientContext& /*ctx*/) {
    auto& ls = static_cast<LimitState&>(raw_state);

    // --- Apply OFFSET ---
    std::vector<uint32_t> src_indices;
    src_indices.reserve(input.count);

    for (size_t i = 0; i < input.count; ++i) {
        if (offset > 0 && ls.rows_skipped < offset) {
            ++ls.rows_skipped;
            continue; // skip this row
        }
        src_indices.push_back(static_cast<uint32_t>(i));
    }

    if (src_indices.empty()) {
        // All rows skipped by offset
        return OperatorResultType::NEED_MORE_INPUT;
    }

    // --- Apply LIMIT ---
    if (limit >= 0) {
        int64_t remaining = limit - ls.rows_emitted;
        if (remaining <= 0) return OperatorResultType::FINISHED;

        if (static_cast<int64_t>(src_indices.size()) > remaining) {
            src_indices.resize(static_cast<size_t>(remaining));
        }
    }

    // Build output
    std::vector<TypeId> types;
    for (auto& col : input.columns) types.push_back(col.type);
    output.Initialize(types);
    DataChunkSlice(output, input, src_indices);

    ls.rows_emitted += static_cast<int64_t>(src_indices.size());

    if (limit >= 0 && ls.rows_emitted >= limit) {
        return OperatorResultType::FINISHED;
    }
    return OperatorResultType::NEED_MORE_INPUT;
}

} // namespace cppcoldb
