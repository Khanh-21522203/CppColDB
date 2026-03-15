#include "execution/operator/physical_drop_table.hpp"
#include "execution/pipeline.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
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
    if (!if_exists) {
        ctx.catalog->DropTable(schema_name, table_name, *ctx.transaction);
    } else {
        try {
            ctx.catalog->DropTable(schema_name, table_name, *ctx.transaction);
        } catch (const CppColDBException&) {
            // table does not exist — silently ignore
        }
    }
    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
