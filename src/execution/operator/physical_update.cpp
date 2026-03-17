#include "execution/operator/physical_update.hpp"
#include "execution/pipeline.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "storage/column/column_chunk.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "common/exception.hpp"
#include "common/types.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalUpdate::CreateScanState() const {
    return std::make_unique<ScanState>();
}

void PhysicalUpdate::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {}

OperatorResultType PhysicalUpdate::GetData(OperatorState& raw_state,
                                            DataChunk& /*output*/,
                                            ClientContext& ctx) {
    auto& state = static_cast<ScanState&>(raw_state);
    if (state.row_offset > 0) return OperatorResultType::FINISHED;
    ++state.row_offset;

    if (!ctx.catalog || !ctx.transaction)
        throw RuntimeError("ClientContext missing catalog/transaction for UPDATE");

    auto* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (!table) throw RuntimeError("UPDATE: table not found: " + table_name);

    // Scan all columns (needed for predicate evaluation and expression computation).
    std::vector<size_t> all_col_ids;
    all_col_ids.reserve(table->columns.size());
    for (size_t i = 0; i < table->columns.size(); ++i) all_col_ids.push_back(i);

    // Per-batch old/new values must stay within STANDARD_VECTOR_SIZE.
    std::vector<TypeId> update_types;
    for (size_t col_id : update_col_ids)
        update_types.push_back(table->columns[col_id].type);

    for (size_t rg_idx = 0; rg_idx < table->row_groups.size(); ++rg_idx) {
        auto& rg = table->row_groups[rg_idx];
        size_t row_offset = 0;

        while (row_offset < rg->RowCount()) {
            DataChunk batch;
            std::vector<uint32_t> batch_offsets;
            rg->ScanBatchWithOffsets(row_offset, all_col_ids, batch, batch_offsets,
                                     *ctx.transaction);
            if (batch.count == 0) continue;

            // Determine which rows in the batch match the predicate.
            std::vector<uint32_t> matching;
            if (predicate) {
                auto sel16 = ExprEvaluator::EvaluatePredicate(*predicate, batch);
                matching.reserve(sel16.size());
                for (uint16_t idx : sel16) matching.push_back(idx);
            } else {
                matching.reserve(batch.count);
                for (uint32_t i = 0; i < (uint32_t)batch.count; ++i) matching.push_back(i);
            }
            if (matching.empty()) continue;

            DataChunk batch_old_values;
            DataChunk batch_new_values;
            batch_old_values.Initialize(update_types);
            batch_new_values.Initialize(update_types);
            std::vector<row_t> batch_row_ids;
            batch_row_ids.reserve(matching.size());

            // For each matching row: compute new values and write in-place.
            for (uint32_t bi : matching) {
                uint32_t local_off = batch_offsets[bi];

                // Build a single-row chunk for this row (for expression evaluation).
                DataChunk single_row;
                std::vector<uint32_t> one_idx = {bi};
                DataChunkSlice(single_row, batch, one_idx);

                // Save old values for undo.
                for (size_t ui = 0; ui < update_col_ids.size(); ++ui) {
                    size_t table_col = update_col_ids[ui];
                    DataVectorAppend(batch_old_values.columns[ui],
                                     batch.columns[table_col], bi);
                }
                batch_old_values.count++;

                // Compute new values via expressions.
                for (size_t ui = 0; ui < update_col_ids.size(); ++ui) {
                    size_t table_col = update_col_ids[ui];
                    DataVector new_val_vec;
                    new_val_vec.Reset(update_types[ui], 0);
                    ExprEvaluator::Evaluate(*update_exprs[ui], single_row, new_val_vec);

                    // Write new value into the row group.
                    auto& chunks = rg->ColumnChunks();
                    chunks[table_col].WriteRow(local_off, new_val_vec, 0);

                    // Save new value for WAL.
                    DataVectorAppend(batch_new_values.columns[ui], new_val_vec, 0);
                }
                batch_new_values.count++;

                // Mark this row as updated in version info.
                rg->GetVersionInfo().MarkUpdated(local_off, ctx.transaction->tx_id);
                batch_row_ids.push_back(MakeRowId(static_cast<RowGroupId>(rg_idx),
                                                  local_off));
            }

            if (!batch_row_ids.empty()) {
                UpdateUndoEntry ue;
                ue.schema     = schema_name;
                ue.table      = table_name;
                ue.row_ids    = std::move(batch_row_ids);
                ue.col_ids    = update_col_ids;
                ue.old_values = std::move(batch_old_values);
                ue.new_values = std::move(batch_new_values);
                ctx.transaction->undo_buffer.PushUpdate(std::move(ue));
            }
        }
    }

    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
