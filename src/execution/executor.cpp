#include "execution/executor.hpp"
#include "execution/pipeline_executor.hpp"
#include "main/client_context.hpp"

#include <unordered_set>
#include <stdexcept>
#include <functional>

namespace cppcoldb {

Executor::Executor(ClientContext& ctx) : ctx_(ctx) {}

void Executor::Initialize(std::unique_ptr<PhysicalOperator> plan) {
    plan_ = std::move(plan);

    // Create the top-level pipeline. The ResultCollector is its sink.
    // Collect schema from plan root's output.
    result_collector_ = std::make_unique<PhysicalResultCollector>(
        plan_->output_names, plan_->output_types);

    auto top_pipeline  = std::make_unique<Pipeline>();
    top_pipeline->sink = result_collector_.get();

    BuildPipelines(plan_.get(), top_pipeline.get());
    pipelines_.push_back(std::move(top_pipeline));
}

void Executor::BuildPipelines(PhysicalOperator* op, Pipeline* current_pipeline) {
    if (!op) return;

    switch (op->role) {
        case OperatorRole::SOURCE:
            current_pipeline->source = op;
            break;

        case OperatorRole::OPERATOR:
            // Prepend: deeper operators (closer to source) are added later → they end
            // up at lower indices after prepend. Source-adjacent ops are at index 0.
            current_pipeline->operators.insert(current_pipeline->operators.begin(), op);
            // TODO (Phase 8): if op is PhysicalHashJoinProbe, create a separate build
            // pipeline for the build side, add it to pipelines_, and register it as a
            // dependency of current_pipeline before recursing into the probe child.
            if (!op->children.empty()) {
                BuildPipelines(op->children[0].get(), current_pipeline);
            }
            break;

        case OperatorRole::SINK:
            current_pipeline->sink = op;
            if (!op->children.empty()) {
                BuildPipelines(op->children[0].get(), current_pipeline);
            }
            break;
    }
}

std::vector<Pipeline*> Executor::TopologicalSort() {
    // Simple: traverse dependency graph using DFS post-order.
    std::vector<Pipeline*>      result;
    std::unordered_set<Pipeline*> visited;

    std::function<void(Pipeline*)> visit = [&](Pipeline* p) {
        if (visited.count(p)) return;
        visited.insert(p);
        for (auto* dep : p->dependencies) visit(dep);
        result.push_back(p);
    };

    for (auto& p : pipelines_) visit(p.get());
    return result;
}

void Executor::Execute() {
    auto ordered = TopologicalSort();
    for (Pipeline* p : ordered) {
        PipelineExecutor pe(ctx_, *p);
        pe.Execute();
    }
}

QueryResult Executor::GetResult() {
    return result_collector_->TakeResult();
}

} // namespace cppcoldb
