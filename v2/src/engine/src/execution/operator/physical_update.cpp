#include "cppcoldb/engine/execution/operator/physical_update.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalUpdate::CreateScanState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

void PhysicalUpdate::InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

OperatorResultType PhysicalUpdate::GetData(OperatorState& state, common::DataChunk& output,
                                            ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
