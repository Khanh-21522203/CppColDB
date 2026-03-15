#pragma once
#include <vector>
#include <string>
#include "execution/physical_operator.hpp"

namespace cppcoldb {

// Holds the results of a query.
struct QueryResult {
    std::vector<std::string> column_names;
    std::vector<TypeId>      column_types;
    std::vector<DataChunk>   chunks;
    bool                     success = true;
    std::string              error_message;

    size_t RowCount() const {
        size_t total = 0;
        for (const auto& c : chunks) total += c.count;
        return total;
    }
};

// SINK operator that accumulates DataChunks into a QueryResult.
class PhysicalResultCollector : public PhysicalOperator {
public:
    PhysicalResultCollector(std::vector<std::string> names,
                            std::vector<TypeId>      types);

    void Consume (const DataChunk& input, OperatorState& state,
                  ClientContext& ctx) override;
    void Finalize(OperatorState& state, ClientContext& ctx) override {}

    // Extract the completed result (call after Execute finishes).
    QueryResult TakeResult() { return std::move(result_); }

private:
    QueryResult result_;
};

} // namespace cppcoldb
