#include "execution/operator/physical_delete.hpp"
#include "execution/pipeline.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "common/exception.hpp"
#include "common/types.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalDelete::CreateScanState() const {
    return std::make_unique<ScanState>();
}

void PhysicalDelete::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {}

OperatorResultType PhysicalDelete::GetData(OperatorState& raw_state,
                                            DataChunk& /*output*/,
                                            ClientContext& ctx) {
    auto& state = static_cast<ScanState&>(raw_state);
    if (state.row_offset > 0) return OperatorResultType::FINISHED;
    ++state.row_offset;

    if (!ctx.catalog || !ctx.transaction)
        throw RuntimeError("ClientContext missing catalog/transaction for DELETE");

    auto* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (!table) throw RuntimeError("DELETE: table not found: " + table_name);

    // Collect column IDs needed for predicate evaluation.
    std::vector<size_t> scan_col_ids;
    if (predicate) {
        for (size_t i = 0; i < table->columns.size(); ++i) scan_col_ids.push_back(i);
    }

    std::vector<row_t> deleted_row_ids;

    for (size_t rg_idx = 0; rg_idx < table->row_groups.size(); ++rg_idx) {
        auto& rg = table->row_groups[rg_idx];
        size_t row_offset = 0;
        while (row_offset < rg->RowCount()) {
            DataChunk batch;
            std::vector<uint32_t> batch_offsets;
            rg->ScanBatchWithOffsets(row_offset, scan_col_ids, batch, batch_offsets,
                                     *ctx.transaction);
            if (batch.count == 0) continue;

            // Determine which rows in the batch match the predicate.
            std::vector<uint32_t> matching; // indices into batch
            if (predicate) {
                auto sel16 = ExprEvaluator::EvaluatePredicate(*predicate, batch);
                for (uint16_t idx : sel16) matching.push_back(idx);
            } else {
                matching.reserve(batch.count);
                for (uint32_t i = 0; i < (uint32_t)batch.count; ++i) matching.push_back(i);
            }

            for (uint32_t bi : matching) {
                uint32_t local_off = batch_offsets[bi];
                rg->GetVersionInfo().MarkDeleted(local_off, ctx.transaction->tx_id);
                deleted_row_ids.push_back(MakeRowId(static_cast<RowGroupId>(rg_idx),
                                                     local_off));
            }
        }
    }

    if (!deleted_row_ids.empty()) {
        DeleteUndoEntry ue;
        ue.schema  = schema_name;
        ue.table   = table_name;
        ue.row_ids = std::move(deleted_row_ids);
        ctx.transaction->undo_buffer.PushDelete(std::move(ue));
    }

    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
