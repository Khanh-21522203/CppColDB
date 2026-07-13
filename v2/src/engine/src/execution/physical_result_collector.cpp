#include "cppcoldb/engine/execution/physical_result_collector.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::execution {

PhysicalResultCollector::PhysicalResultCollector(std::vector<std::string> /*names*/,
                                                   std::vector<common::TypeId> /*types*/) {}

void PhysicalResultCollector::Consume(const common::DataChunk& input, OperatorState& state,
                                       ::cppcoldb::ClientContext& ctx) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::execution
