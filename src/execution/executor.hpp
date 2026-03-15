#pragma once
#include <vector>
#include <memory>
#include "execution/pipeline.hpp"
#include "execution/physical_result_collector.hpp"

namespace cppcoldb {

struct ClientContext;

class Executor {
public:
    explicit Executor(ClientContext& ctx);

    // Build pipelines from the physical plan root.
    void Initialize(std::unique_ptr<PhysicalOperator> plan);

    // Execute all pipelines in dependency order.
    void Execute();

    // Retrieve accumulated result after Execute() completes.
    QueryResult GetResult();

private:
    // Recursive pipeline builder: walk the operator tree, populating current_pipeline.
    void BuildPipelines(PhysicalOperator* op, Pipeline* current_pipeline);

    // Topological sort: return pipelines in execution order (dependencies first).
    std::vector<Pipeline*> TopologicalSort();

    ClientContext&                           ctx_;
    std::unique_ptr<PhysicalOperator>        plan_;
    std::vector<std::unique_ptr<Pipeline>>   pipelines_;
    std::unique_ptr<PhysicalResultCollector> result_collector_;
};

} // namespace cppcoldb
