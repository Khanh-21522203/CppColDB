#include "execution/operator/physical_hash_join.hpp"

namespace cppcoldb {

// ---------------------------------------------------------------------------
// PhysicalHashJoinBuild::Consume
// ---------------------------------------------------------------------------

void PhysicalHashJoinBuild::Consume(const DataChunk& input, OperatorState& /*state*/,
                                    ClientContext& /*ctx*/) {
    for (size_t row = 0; row < input.count; ++row) {
        ht->Insert(key_col_idxs, input, row);
    }
}

// ---------------------------------------------------------------------------
// PhysicalHashJoinProbe::Execute
// Iterates left-side input rows, probes the hash table, and emits
// concatenated (left || right) output rows.
// Returns HAVE_MORE_OUTPUT when the output chunk fills up mid-batch.
// Returns NEED_MORE_INPUT when all input rows have been processed.
// ---------------------------------------------------------------------------

// Extract a Value from a DataVector at row_idx.
static Value ReadRowValue(const DataVector& vec, size_t row_idx) {
    if (vec.IsNull(row_idx)) return Value::Null(vec.type);
    switch (vec.type) {
        case TypeId::BOOLEAN:
            return Value::Boolean(vec.int_data[row_idx] != 0);
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:
            return Value{vec.type, vec.int_data[row_idx]};
        case TypeId::FLOAT32:
        case TypeId::FLOAT64:
            return Value{vec.type, vec.float_data[row_idx]};
        case TypeId::VARCHAR:
            return Value{vec.type, vec.str_data[row_idx]};
        default:
            return Value::Null(vec.type);
    }
}

// Append one combined (left row || right row) to the output chunk.
static void EmitMatchRow(DataChunk& out,
                         const DataChunk& left, size_t left_row,
                         const std::vector<Value>& right_row) {
    size_t left_cols = left.columns.size();
    for (size_t c = 0; c < left_cols; ++c) {
        DataVectorAppend(out.columns[c], left.columns[c], left_row);
    }
    for (size_t c = 0; c < right_row.size(); ++c) {
        DataVectorAppendValue(out.columns[left_cols + c], right_row[c]);
    }
    ++out.count;
}

OperatorResultType PhysicalHashJoinProbe::Execute(const DataChunk& input, DataChunk& output,
                                                  OperatorState& raw_state,
                                                  ClientContext& /*ctx*/) {
    auto& state = static_cast<JoinProbeState&>(raw_state);

    output.Initialize(output_types);
    output.count = 0;
    // Pre-size column backing to STANDARD_VECTOR_SIZE so EmitMatchRow never triggers
    // incremental resize (avoids ~10 realloc+copy cycles per column per Execute call).
    for (auto& col : output.columns) {
        switch (col.type) {
            case TypeId::BOOLEAN:
            case TypeId::INT8: case TypeId::INT16: case TypeId::INT32: case TypeId::INT64:
                col.int_data.resize(STANDARD_VECTOR_SIZE, 0);  break;
            case TypeId::FLOAT32: case TypeId::FLOAT64:
                col.float_data.resize(STANDARD_VECTOR_SIZE, 0.0); break;
            case TypeId::VARCHAR:
                col.str_data.resize(STANDARD_VECTOR_SIZE, ""); break;
            default: break;
        }
        col.count = 0;
    }

    // Pre-size probe_key once per state lifetime.
    if (state.probe_key.size() != left_key_col_idxs.size()) {
        state.probe_key.assign(left_key_col_idxs.size(), Value{});
    }

    // Phase 1: Batch-compute hashes for all input rows directly from typed arrays.
    // This replaces per-row Value boxing + HashKey() variant dispatch.
    size_t hashes[STANDARD_VECTOR_SIZE];
    ht->ComputeHashes(input, left_key_col_idxs, hashes, input.count);

    size_t row  = state.probe_row_idx;
    size_t midx = state.match_idx;

    while (row < input.count) {
        // Phase 2: Lookup precomputed hash — no per-row hash recomputation.
        const auto& candidates = ht->ProbeByHash(hashes[row]);

        // Skip rows with no matches entirely (no probe_key build needed).
        if (candidates.empty()) {
            ++row;
            midx = 0;
            continue;
        }

        // Phase 3: Equality check — build probe_key only for rows that have candidates.
        for (size_t ki = 0; ki < left_key_col_idxs.size(); ++ki) {
            state.probe_key[ki] = ReadRowValue(input.columns[left_key_col_idxs[ki]], row);
        }
        const auto& probe_key = state.probe_key;
        while (midx < candidates.size()) {
            // Verify full key equality (guard against hash collisions).
            bool eq = true;
            for (size_t k = 0; k < probe_key.size() && eq; ++k) {
                if (!(probe_key[k] == candidates[midx].key[k])) eq = false;
            }
            if (eq) {
                EmitMatchRow(output, input, row, candidates[midx].row);
            }
            ++midx;
            if (output.count == STANDARD_VECTOR_SIZE) {
                state.probe_row_idx = row;
                state.match_idx     = midx;
                return OperatorResultType::HAVE_MORE_OUTPUT;
            }
        }

        ++row;
        midx = 0;
    }

    // All input rows processed.
    state.probe_row_idx = 0;
    state.match_idx     = 0;
    return OperatorResultType::NEED_MORE_INPUT;
}

} // namespace cppcoldb
