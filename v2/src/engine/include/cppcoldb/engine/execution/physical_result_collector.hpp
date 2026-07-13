#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/execution/physical_operator.hpp"
#include "cppcoldb/engine/profiler/profiling_result.hpp"

namespace cppcoldb::engine::execution {

// The materialized output of one executed query: its schema, its row
// chunks, and (on failure) an error message. Optionally carries a
// ProfilingResult when the query ran with profiling enabled.
struct QueryResult {
    std::vector<std::string>                          column_names;
    std::vector<common::TypeId>                        column_types;
    std::vector<common::DataChunk>                      chunks;
    bool                                                success = true;
    std::string                                         error_message;
    std::optional<::cppcoldb::engine::profiler::ProfilingResult> profiling_result;

    std::size_t RowCount() const {
        std::size_t total = 0;
        for (const auto& c : chunks) total += c.count;
        return total;
    }
};

// Terminal SINK operator of every executed plan: gathers the chunks produced
// by the rest of the pipeline into one QueryResult, handed off via
// TakeResult().
class PhysicalResultCollector : public PhysicalOperator {
public:
    PhysicalResultCollector(std::vector<std::string> names, std::vector<common::TypeId> types);

    void Consume(const common::DataChunk& input, OperatorState& state,
                 ::cppcoldb::ClientContext& ctx) override;
    void Finalize(OperatorState& state, ::cppcoldb::ClientContext& ctx) override {}

    QueryResult TakeResult() { return std::move(result_); }

private:
    QueryResult result_;
};

} // namespace cppcoldb::engine::execution
