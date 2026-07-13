#include "cppcoldb/engine/execution/operator/physical_hash_join.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

void PhysicalHashJoinBuild::Consume(const common::DataChunk& input, OperatorState& state,
                                     ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

OperatorResultType PhysicalHashJoinProbe::Execute(const common::DataChunk& input, common::DataChunk& output,
                                                   OperatorState& state, ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
