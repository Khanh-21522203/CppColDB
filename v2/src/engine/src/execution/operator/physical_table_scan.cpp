#include "cppcoldb/engine/execution/operator/physical_table_scan.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalTableScan::CreateScanState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

void PhysicalTableScan::InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

OperatorResultType PhysicalTableScan::GetData(OperatorState& state, common::DataChunk& output,
                                               ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
