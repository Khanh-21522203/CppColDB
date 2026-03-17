#include "execution/operator/physical_insert.hpp"
#include "execution/pipeline.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalInsert::CreateScanState() const {
    return std::make_unique<ScanState>();
}

void PhysicalInsert::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {}

OperatorResultType PhysicalInsert::GetData(OperatorState& raw_state,
                                            DataChunk& /*output*/,
                                            ClientContext& ctx) {
    auto& state = static_cast<ScanState&>(raw_state);
    if (state.row_offset > 0) return OperatorResultType::FINISHED; // already executed
    ++state.row_offset;

    if (!ctx.catalog || !ctx.transaction) {
        throw RuntimeError("ClientContext missing catalog/transaction for INSERT");
    }

    if (rows.count == 0) return OperatorResultType::FINISHED;

    auto* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (!table) {
        throw RuntimeError("INSERT: table not found: " + table_name);
    }

    RowGroup* rg          = table->GetOrAddRowGroup();
    size_t    rg_id       = table->row_groups.size() - 1;
    size_t    append_start = rg->RowCount();

    rg->Append(rows, column_ids, ctx.transaction->tx_id);

    InsertUndoEntry ue;
    ue.schema       = schema_name;
    ue.table        = table_name;
    ue.row_group_id = rg_id;
    ue.append_start = append_start;
    ue.append_count = rows.count;
    ctx.transaction->undo_buffer.PushInsert(std::move(ue));

    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
