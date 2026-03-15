#pragma once
#include <memory>
#include <set>
#include <vector>
#include "planner/logical_plan/logical_plan.hpp"

namespace cppcoldb {

struct OptimizerConfig {
    bool constant_folding   = true;
    bool predicate_pushdown = true;
    bool column_pruning     = true;
    bool filter_merge       = true;
};

class Optimizer {
public:
    explicit Optimizer(OptimizerConfig cfg = {});

    // Run all enabled passes on the plan. Returns the (possibly new) root.
    std::unique_ptr<LogicalPlan> Optimize(std::unique_ptr<LogicalPlan> plan);

private:
    // Pass 1: Constant folding — fold compile-time-known expressions
    std::unique_ptr<LogicalPlan> RunConstantFolding  (std::unique_ptr<LogicalPlan> plan);
    // Pass 2: Predicate pushdown — move filters toward the scan
    std::unique_ptr<LogicalPlan> RunPredicatePushdown(std::unique_ptr<LogicalPlan> plan);
    // Pass 3: Column pruning — restrict LogicalGet.column_ids to referenced columns only
    std::unique_ptr<LogicalPlan> RunColumnPruning    (std::unique_ptr<LogicalPlan> plan);
    // Pass 4: Filter merge — combine stacked LogicalFilter nodes into one AND predicate
    std::unique_ptr<LogicalPlan> RunFilterMerge      (std::unique_ptr<LogicalPlan> plan);

    // Helpers for constant folding
    std::unique_ptr<LogicalExpr> FoldExpr(std::unique_ptr<LogicalExpr> e);
    std::unique_ptr<LogicalPlan> FoldPlan(std::unique_ptr<LogicalPlan> plan);

    // Helpers for predicate pushdown
    // Returns new root after pushing predicates; pending is consumed.
    std::unique_ptr<LogicalPlan> Pushdown(
        std::unique_ptr<LogicalPlan> node,
        std::vector<std::unique_ptr<LogicalExpr>>& pending);

    // Helpers for column pruning
    // required_cols: set of column_idx values needed by ancestors
    void CollectColumnRefs(const LogicalExpr& e, std::set<size_t>& out);
    std::unique_ptr<LogicalPlan> PruneColumns(
        std::unique_ptr<LogicalPlan> plan,
        std::set<size_t>& required_cols);

    // Helpers for filter merge
    std::unique_ptr<LogicalPlan> MergeFilters(std::unique_ptr<LogicalPlan> plan);

    // Returns true for DDL/DML plans that should pass through optimizer unchanged.
    bool IsTrivialPlan(const LogicalPlan& plan);

    OptimizerConfig cfg_;
};

} // namespace cppcoldb
