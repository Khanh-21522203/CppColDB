#include "cppcoldb/engine/execution/operator/physical_create_table.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalCreateTable::CreateScanState() const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void PhysicalCreateTable::InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

OperatorResultType PhysicalCreateTable::GetData(OperatorState& state, common::DataChunk& output,
                                                 ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
