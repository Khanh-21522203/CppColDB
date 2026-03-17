#include "execution/executor.hpp"
#include "execution/pipeline_executor.hpp"
#include "execution/operator/physical_hash_aggregation.hpp"
#include "execution/operator/physical_hash_join.hpp"
#include "main/client_context.hpp"

#include <unordered_set>
#include <stdexcept>
#include <functional>
#include <typeinfo>
#include <cxxabi.h>
#include <cstdlib>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Operator name demangling
// ---------------------------------------------------------------------------
static std::string DemangledName(const char* mangled) {
    int   status    = 0;
    char* demangled = abi::__cxa_demangle(mangled, nullptr, nullptr, &status);
    std::string result = (status == 0 && demangled) ? demangled : mangled;
    if (demangled) std::free(demangled);
    // Strip namespace prefix: take the part after the last "::".
    auto pos = result.rfind("::");
    if (pos != std::string::npos) result = result.substr(pos + 2);
    return result;
}

// DFS over the operator tree, registering every operator with the profiler.
// Mirrors BuildPipelines' handling of hidden children (build_op, source_op).
static void RegisterOperators(PhysicalOperator* op, QueryProfiler& profiler) {
    if (!op) return;
    op->profile_idx = static_cast<int>(
        profiler.RegisterOperator(DemangledName(typeid(*op).name())));
    for (auto& child : op->children) {
        RegisterOperators(child.get(), profiler);
    }
    // Hidden children not in op->children:
    if (auto* probe = dynamic_cast<PhysicalHashJoinProbe*>(op)) {
        RegisterOperators(probe->build_op.get(), profiler);
    }
    if (auto* agg = dynamic_cast<PhysicalHashAggregation*>(op)) {
        RegisterOperators(agg->source_op.get(), profiler);
    }
}

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

            if (auto* probe = dynamic_cast<PhysicalHashJoinProbe*>(op)) {
                // Two-pipeline hash join pattern:
                //   build_pipeline: right_scan → PhysicalHashJoinBuild (SINK)
                //   current_pipeline: left_scan → PhysicalHashJoinProbe (OPERATOR) → sink
                // The probe pipeline depends on the build pipeline completing first.
                auto build_pipeline      = std::make_unique<Pipeline>();
                build_pipeline->sink     = probe->build_op.get();
                if (!probe->build_op->children.empty()) {
                    BuildPipelines(probe->build_op->children[0].get(), build_pipeline.get());
                }
                current_pipeline->dependencies.push_back(build_pipeline.get());
                pipelines_.push_back(std::move(build_pipeline));
            }

            // Recurse into probe/left child for the current (probe) pipeline.
            if (!op->children.empty()) {
                BuildPipelines(op->children[0].get(), current_pipeline);
            }
            break;

        case OperatorRole::SINK:
            if (auto* agg = dynamic_cast<PhysicalHashAggregation*>(op)) {
                // Two-pipeline aggregation pattern:
                //   consume_pipeline: scan/filter → PhysicalHashAggregation (SINK)
                //   current_pipeline: PhysicalAggregationSource (SOURCE) → ... → ResultCollector (already set)
                auto consume_pipeline = std::make_unique<Pipeline>();
                consume_pipeline->sink = op;
                if (!op->children.empty()) {
                    BuildPipelines(op->children[0].get(), consume_pipeline.get());
                }
                current_pipeline->dependencies.push_back(consume_pipeline.get());
                current_pipeline->source = agg->source_op.get();
                pipelines_.push_back(std::move(consume_pipeline));
                // Do NOT recurse into children for current_pipeline — source already set.
            } else {
                current_pipeline->sink = op;
                if (!op->children.empty()) {
                    BuildPipelines(op->children[0].get(), current_pipeline);
                }
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
    // Register all operators with the profiler before running any pipeline.
    if (ctx_.profiler_.IsActive()) {
        RegisterOperators(plan_.get(), ctx_.profiler_);
    }

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
