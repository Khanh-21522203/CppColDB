#include "cppcoldb/engine/execution/operator/physical_drop_table.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalDropTable::CreateScanState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

void PhysicalDropTable::InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

OperatorResultType PhysicalDropTable::GetData(OperatorState& state, common::DataChunk& output,
                                               ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
