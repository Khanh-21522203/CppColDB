#include "execution/physical_result_collector.hpp"

namespace cppcoldb {

PhysicalResultCollector::PhysicalResultCollector(std::vector<std::string> names,
                                                  std::vector<TypeId>      types) {
    role = OperatorRole::SINK;
    result_.column_names = std::move(names);
    result_.column_types = std::move(types);
}

void PhysicalResultCollector::Consume(const DataChunk& input, OperatorState& /*state*/,
                                       ClientContext& /*ctx*/) {
    if (input.count == 0) return;
    DataChunk copy;
    copy.Initialize(result_.column_types);
    std::vector<uint32_t> all_rows;
    all_rows.reserve(input.count);
    for (uint32_t i = 0; i < static_cast<uint32_t>(input.count); ++i)
        all_rows.push_back(i);
    DataChunkSlice(copy, input, all_rows);
    result_.chunks.push_back(std::move(copy));
}

} // namespace cppcoldb
