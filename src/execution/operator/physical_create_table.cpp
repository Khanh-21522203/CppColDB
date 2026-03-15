#include "execution/operator/physical_create_table.hpp"
#include "execution/pipeline.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
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
    if (!if_not_exists) {
        ctx.catalog->CreateTable(schema_name, table_name, columns, *ctx.transaction);
    } else {
        // if_not_exists: only create if not already there
        try {
            ctx.catalog->CreateTable(schema_name, table_name, columns, *ctx.transaction);
        } catch (const CppColDBException&) {
            // table already exists — silently ignore
        }
    }
    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
