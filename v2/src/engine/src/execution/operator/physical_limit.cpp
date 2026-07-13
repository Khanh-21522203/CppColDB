#include "cppcoldb/engine/execution/operator/physical_limit.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalLimit::CreateOperatorState() const { CPPCOLDB_NOT_IMPLEMENTED(); }

OperatorResultType PhysicalLimit::Execute(const common::DataChunk& input, common::DataChunk& output,
                                           OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
