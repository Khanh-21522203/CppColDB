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
#include <unordered_map>

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

    std::vector<size_t> effective_scan_col_ids = scan_col_ids;
    if (effective_scan_col_ids.empty()) {
        effective_scan_col_ids = update_col_ids;
    }

    std::unordered_map<size_t, size_t> table_col_to_scan_idx;
    table_col_to_scan_idx.reserve(effective_scan_col_ids.size());
    for (size_t i = 0; i < effective_scan_col_ids.size(); ++i) {
        table_col_to_scan_idx[effective_scan_col_ids[i]] = i;
    }

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
            rg->ScanBatchWithOffsets(row_offset, effective_scan_col_ids, batch, batch_offsets,
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

            std::vector<uint32_t> matched_offsets;
            matched_offsets.reserve(matching.size());
            std::vector<row_t> batch_row_ids;
            batch_row_ids.reserve(matching.size());
            for (uint32_t bi : matching) {
                uint32_t local_off = batch_offsets[bi];
                matched_offsets.push_back(local_off);
                rg->GetVersionInfo().MarkUpdated(local_off, ctx.transaction->tx_id);
                batch_row_ids.push_back(
                    MakeRowId(static_cast<RowGroupId>(rg_idx), local_off));
            }

            DataChunk batch_old_values;
            DataChunk batch_new_values;
            batch_old_values.Initialize(update_types);
            batch_new_values.Initialize(update_types);
            auto& chunks = rg->ColumnChunks();
            for (size_t ui = 0; ui < update_col_ids.size(); ++ui) {
                size_t table_col = update_col_ids[ui];
                auto it = table_col_to_scan_idx.find(table_col);
                if (it == table_col_to_scan_idx.end()) {
                    throw RuntimeError("UPDATE: missing scanned value for updated column");
                }
                size_t scan_idx = it->second;

                // Save old values for undo.
                for (uint32_t bi : matching) {
                    DataVectorAppend(batch_old_values.columns[ui],
                                     batch.columns[scan_idx], bi);
                }

                // Evaluate SET expr once for the whole batch, then select matching rows.
                DataVector full_new_vec;
                ExprEvaluator::Evaluate(*update_exprs[ui], batch, full_new_vec);

                DataVector selected_new_vec;
                selected_new_vec.Reset(update_types[ui], 0);
                for (uint32_t bi : matching) {
                    DataVectorAppend(selected_new_vec, full_new_vec, bi);
                }

                // Write selected values in batch and keep redo values for WAL.
                chunks[table_col].WriteRows(matched_offsets, selected_new_vec);
                batch_new_values.columns[ui] = std::move(selected_new_vec);
            }

            batch_old_values.count = matching.size();
            batch_new_values.count = matching.size();

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
