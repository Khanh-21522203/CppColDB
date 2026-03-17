#include "execution/operator/physical_drop_table.hpp"
#include "execution/pipeline.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalDropTable::CreateScanState() const {
    return std::make_unique<ScanState>();
}

void PhysicalDropTable::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {}

OperatorResultType PhysicalDropTable::GetData(OperatorState& raw_state,
                                               DataChunk& /*output*/,
                                               ClientContext& ctx) {
    auto& state = static_cast<ScanState&>(raw_state);
    if (state.row_offset > 0) return OperatorResultType::FINISHED;
    ++state.row_offset;

    if (!ctx.catalog || !ctx.transaction) {
        throw RuntimeError("ClientContext missing catalog/transaction for DROP TABLE");
    }

    bool dropped = false;
    if (!if_exists) {
        ctx.catalog->DropTable(schema_name, table_name, *ctx.transaction);
        dropped = true;
    } else {
        try {
            ctx.catalog->DropTable(schema_name, table_name, *ctx.transaction);
            dropped = true;
        } catch (const CppColDBException&) {
            // table does not exist — silently ignore
        }
    }

    // Register in undo buffer so TransactionManager::Commit calls catalog_.CommitEntry,
    // which sets delete_commit_time and makes the drop visible to future transactions.
    if (dropped) {
        CatalogUndoEntry ue;
        ue.schema     = schema_name;
        ue.table      = table_name;
        ue.was_create = false;
        ctx.transaction->undo_buffer.PushCatalogEntry(std::move(ue));
    }

    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
