#include "execution/operator/physical_create_table.hpp"
#include "execution/pipeline.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

std::unique_ptr<OperatorState> PhysicalCreateTable::CreateScanState() const {
    return std::make_unique<ScanState>();
}

void PhysicalCreateTable::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {}

OperatorResultType PhysicalCreateTable::GetData(OperatorState& raw_state,
                                                  DataChunk& /*output*/,
                                                  ClientContext& ctx) {
    auto& state = static_cast<ScanState&>(raw_state);
    if (state.row_offset > 0) return OperatorResultType::FINISHED; // already executed
    ++state.row_offset;

    if (!ctx.catalog || !ctx.transaction) {
        throw RuntimeError("ClientContext missing catalog/transaction for CREATE TABLE");
    }

    bool created = false;
    if (!if_not_exists) {
        ctx.catalog->CreateTable(schema_name, table_name, columns, *ctx.transaction);
        created = true;
    } else {
        // if_not_exists: only create if not already there
        try {
            ctx.catalog->CreateTable(schema_name, table_name, columns, *ctx.transaction);
            created = true;
        } catch (const CppColDBException&) {
            // table already exists — silently ignore
        }
    }

    // Register in undo buffer so TransactionManager::Commit calls catalog_.CommitEntry,
    // which sets create_commit_time and makes the table visible to future transactions.
    if (created) {
        CatalogUndoEntry ue;
        ue.schema     = schema_name;
        ue.table      = table_name;
        ue.was_create = true;
        ue.col_names.reserve(columns.size());
        ue.col_types.reserve(columns.size());
        for (const auto& col : columns) {
            ue.col_names.push_back(col.name);
            ue.col_types.push_back(col.type);
        }
        ctx.transaction->undo_buffer.PushCatalogEntry(std::move(ue));
    }

    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
