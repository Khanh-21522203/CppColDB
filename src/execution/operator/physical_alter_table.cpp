#include "execution/operator/physical_alter_table.hpp"
#include "execution/pipeline.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalAlterTable::CreateScanState() const {
    return std::make_unique<ScanState>();
}

void PhysicalAlterTable::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {}

OperatorResultType PhysicalAlterTable::GetData(OperatorState& raw_state,
                                                DataChunk& /*output*/,
                                                ClientContext& ctx) {
    auto& state = static_cast<ScanState&>(raw_state);
    if (state.row_offset > 0) return OperatorResultType::FINISHED;
    ++state.row_offset;

    if (!ctx.catalog || !ctx.transaction)
        throw RuntimeError("ClientContext missing catalog/transaction for ALTER TABLE");

    AlterPartitionUndoEntry ue;
    ue.schema = schema_name;
    ue.table  = table_name;

    if (kind == LogicalAlterTable::AlterKind::DROP_PARTITION) {
        ue.kind = AlterPartitionUndoEntry::AlterKind::DROP_PARTITION;
        ctx.catalog->DropPartition(schema_name, table_name, partition_idx,
                                   *ctx.transaction,
                                   ue.old_partition_info,
                                   ue.old_partition_rg_indices);
    } else {
        ue.kind = AlterPartitionUndoEntry::AlterKind::ADD_PARTITION;
        ctx.catalog->AddPartition(schema_name, table_name, new_bound, new_values,
                                  *ctx.transaction,
                                  ue.old_partition_info,
                                  ue.old_partition_rg_indices);
    }

    // Capture new state for WAL redo.
    auto* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (table) {
        ue.new_partition_info       = table->partition_info;
        ue.new_partition_rg_indices = table->partition_rg_indices;
    }

    ctx.transaction->undo_buffer.PushAlterPartition(std::move(ue));
    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
