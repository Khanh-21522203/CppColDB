#pragma once

#include <memory>
#include <vector>

#include "cppcoldb/engine/execution/physical_result_collector.hpp"
#include "cppcoldb/engine/execution/pipeline.hpp"

namespace cppcoldb {
class ClientContext;
} // namespace cppcoldb

namespace cppcoldb::engine::execution {

// Owns one physical plan's lifetime: splits it into Pipelines at every SINK
// boundary, runs them in dependency order, and hands back the collected
// QueryResult.
class Executor {
public:
    explicit Executor(::cppcoldb::ClientContext& ctx);

    void Initialize(std::unique_ptr<PhysicalOperator> plan);
    void Execute();
    QueryResult GetResult();

private:
    void BuildPipelines(PhysicalOperator* op, Pipeline* current_pipeline);
    std::vector<Pipeline*> TopologicalSort();

    ::cppcoldb::ClientContext&               ctx_;
    std::unique_ptr<PhysicalOperator>        plan_;
    std::vector<std::unique_ptr<Pipeline>>   pipelines_;
    std::unique_ptr<PhysicalResultCollector> result_collector_;
};

} // namespace cppcoldb::engine::execution
