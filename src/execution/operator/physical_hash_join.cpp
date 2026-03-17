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

    size_t row  = state.probe_row_idx;
    size_t midx = state.match_idx;

    while (row < input.count) {
        // Build probe key for this left input row.
        std::vector<Value> probe_key;
        for (size_t kc : left_key_col_idxs) {
            probe_key.push_back(ReadRowValue(input.columns[kc], row));
        }

        const auto& candidates = ht->Probe(probe_key);
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
