#include "cppcoldb/engine/execution/operator/physical_delete.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalDelete::CreateScanState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

void PhysicalDelete::InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

OperatorResultType PhysicalDelete::GetData(OperatorState& state, common::DataChunk& output,
                                            ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
