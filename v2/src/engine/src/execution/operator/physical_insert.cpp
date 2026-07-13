#include "cppcoldb/engine/execution/operator/physical_insert.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalInsert::CreateScanState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

std::unique_ptr<OperatorState> PhysicalInsert::CreateSinkState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

void PhysicalInsert::InitScan(OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

OperatorResultType PhysicalInsert::GetData(OperatorState& state, common::DataChunk& output,
                                            ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void PhysicalInsert::Consume(const common::DataChunk& input, OperatorState& state,
                              ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
