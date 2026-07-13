#include "cppcoldb/engine/execution/operator/physical_aggregation_source.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

std::unique_ptr<OperatorState> PhysicalAggregationSource::CreateScanState() const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

OperatorResultType PhysicalAggregationSource::GetData(OperatorState& state, common::DataChunk& output,
                                                       ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
