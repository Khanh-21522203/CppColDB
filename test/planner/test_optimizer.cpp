#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <stdexcept>

#include "common/types.hpp"
#include "common/exception.hpp"
#include "catalog/catalog_entry.hpp"
#include "planner/logical_plan/logical_plan.hpp"
#include "planner/optimizer.hpp"

using namespace cppcoldb;

// ---------------------------------------------------------------------------
// Helper: build a simple LogicalGet for an n-column INT64 table
// ---------------------------------------------------------------------------

static std::unique_ptr<LogicalGet> MakeGet(const std::string& tname = "t",
                                            size_t n_cols = 1) {
    auto get = std::make_unique<LogicalGet>();
    get->schema_name = "main";
    get->table_name  = tname;
    for (size_t i = 0; i < n_cols; ++i) {
        get->column_ids.push_back(i);
        get->output_types.push_back(TypeId::INT64);
        get->output_names.push_back("col" + std::to_string(i));
    }
    get->catalog_row_count = 1000;
    return get;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestConstantFoldArithmetic() {
    // Projection: [1 + 2]  →  should fold to [3]
    auto proj = std::make_unique<LogicalProjection>();

    auto lit1 = std::make_unique<LogicalLit>();
    lit1->value       = Value::Integer(1);
    lit1->result_type = TypeId::INT64;

    auto lit2 = std::make_unique<LogicalLit>();
    lit2->value       = Value::Integer(2);
    lit2->result_type = TypeId::INT64;

    auto add = std::make_unique<LogicalBinaryOp>();
    add->op          = "+";
    add->result_type = TypeId::INT64;
    add->left        = std::move(lit1);
    add->right       = std::move(lit2);

    proj->exprs.push_back(std::move(add));
    proj->output_types = {TypeId::INT64};
    proj->output_names = {"r"};
    proj->children.push_back(MakeGet());

    Optimizer opt;
    auto result = opt.Optimize(std::move(proj));

    assert(result->node_type == LogicalPlan::Type::PROJECTION);
    auto& rproj = static_cast<LogicalProjection&>(*result);
    assert(rproj.exprs[0]->kind == LogicalExpr::Kind::LITERAL);
    auto& lit = static_cast<LogicalLit&>(*rproj.exprs[0]);
    assert(lit.value.GetInt64() == 3);

    std::cout << "TestConstantFoldArithmetic: PASS\n";
}

static void TestConstantFoldTrueFilter() {
    // Filter: WHERE 1 = 1  → filter removed, GET is top
    auto filter = std::make_unique<LogicalFilter>();

    auto l = std::make_unique<LogicalLit>();
    l->value       = Value::Integer(1);
    l->result_type = TypeId::INT64;

    auto r = std::make_unique<LogicalLit>();
    r->value       = Value::Integer(1);
    r->result_type = TypeId::INT64;

    auto eq = std::make_unique<LogicalBinaryOp>();
    eq->op          = "=";
    eq->result_type = TypeId::BOOLEAN;
    eq->left        = std::move(l);
    eq->right       = std::move(r);

    filter->predicate    = std::move(eq);
    filter->output_types = {TypeId::INT64};
    filter->output_names = {"col0"};
    filter->children.push_back(MakeGet());

    Optimizer opt;
    auto result = opt.Optimize(std::move(filter));

    // After folding: 1=1 → true → filter removed → GET is top
    assert(result->node_type == LogicalPlan::Type::GET);

    std::cout << "TestConstantFoldTrueFilter: PASS\n";
}

static void TestPredicatePushdown() {
    // Projection → Filter → Get
    // After pushdown: Filter should move into Get.pushed_filters
    auto get = MakeGet();

    auto filter = std::make_unique<LogicalFilter>();
    auto col = std::make_unique<BoundColumnRef>();
    col->column_idx  = 0;
    col->result_type = TypeId::INT64;

    auto lit = std::make_unique<LogicalLit>();
    lit->value       = Value::Integer(5);
    lit->result_type = TypeId::INT64;

    auto gt = std::make_unique<LogicalBinaryOp>();
    gt->op          = ">";
    gt->result_type = TypeId::BOOLEAN;
    gt->left        = std::move(col);
    gt->right       = std::move(lit);

    filter->predicate    = std::move(gt);
    filter->output_types = {TypeId::INT64};
    filter->output_names = {"col0"};
    filter->children.push_back(std::move(get));

    auto proj = std::make_unique<LogicalProjection>();
    auto col2 = std::make_unique<BoundColumnRef>();
    col2->column_idx  = 0;
    col2->result_type = TypeId::INT64;
    proj->exprs.push_back(std::move(col2));
    proj->output_types = {TypeId::INT64};
    proj->output_names = {"col0"};
    proj->children.push_back(std::move(filter));

    // Disable column pruning to isolate pushdown test
    OptimizerConfig cfg;
    cfg.column_pruning = false;
    Optimizer opt(cfg);
    auto result = opt.Optimize(std::move(proj));

    // After pushdown: no FILTER node, GET has pushed_filters
    assert(result->node_type == LogicalPlan::Type::PROJECTION);
    auto& rproj = static_cast<LogicalProjection&>(*result);
    assert(rproj.children[0]->node_type == LogicalPlan::Type::GET);
    auto& rget = static_cast<LogicalGet&>(*rproj.children[0]);
    assert(!rget.pushed_filters.empty());

    std::cout << "TestPredicatePushdown: PASS\n";
}

static void TestColumnPruning() {
    // SELECT col0 FROM 3-column table  →  LogicalGet.column_ids should be pruned to {0}
    auto get  = MakeGet("t", 3);

    auto proj = std::make_unique<LogicalProjection>();
    auto col  = std::make_unique<BoundColumnRef>();
    col->column_idx  = 0;
    col->result_type = TypeId::INT64;
    proj->exprs.push_back(std::move(col));
    proj->output_types = {TypeId::INT64};
    proj->output_names = {"col0"};
    proj->children.push_back(std::move(get));

    OptimizerConfig cfg;
    cfg.predicate_pushdown = false;
    cfg.constant_folding   = false;
    Optimizer opt(cfg);
    auto result = opt.Optimize(std::move(proj));

    assert(result->node_type == LogicalPlan::Type::PROJECTION);
    auto& rget = static_cast<LogicalGet&>(*result->children[0]);
    assert(rget.column_ids.size() == 1);
    assert(rget.column_ids[0] == 0);

    std::cout << "TestColumnPruning: PASS\n";
}

static void TestFilterMerge() {
    // Filter(a > 5) → Filter(b < 10) → Get  →  after merge: Filter(a>5 AND b<10) → Get

    // Build inner filter: col1 < 10
    auto get = MakeGet("t", 2);

    auto inner = std::make_unique<LogicalFilter>();
    auto col1  = std::make_unique<BoundColumnRef>();
    col1->column_idx  = 1;
    col1->result_type = TypeId::INT64;

    auto lit1 = std::make_unique<LogicalLit>();
    lit1->value       = Value::Integer(10);
    lit1->result_type = TypeId::INT64;

    auto lt = std::make_unique<LogicalBinaryOp>();
    lt->op          = "<";
    lt->result_type = TypeId::BOOLEAN;
    lt->left        = std::move(col1);
    lt->right       = std::move(lit1);

    inner->predicate    = std::move(lt);
    inner->output_types = {TypeId::INT64, TypeId::INT64};
    inner->output_names = {"c0", "c1"};
    inner->children.push_back(std::move(get));

    // Build outer filter: col0 > 5
    auto outer = std::make_unique<LogicalFilter>();
    auto col0  = std::make_unique<BoundColumnRef>();
    col0->column_idx  = 0;
    col0->result_type = TypeId::INT64;

    auto lit0 = std::make_unique<LogicalLit>();
    lit0->value       = Value::Integer(5);
    lit0->result_type = TypeId::INT64;

    auto gt = std::make_unique<LogicalBinaryOp>();
    gt->op          = ">";
    gt->result_type = TypeId::BOOLEAN;
    gt->left        = std::move(col0);
    gt->right       = std::move(lit0);

    outer->predicate    = std::move(gt);
    outer->output_types = {TypeId::INT64, TypeId::INT64};
    outer->output_names = {"c0", "c1"};
    outer->children.push_back(std::move(inner));

    OptimizerConfig cfg;
    cfg.constant_folding   = false;
    cfg.predicate_pushdown = false;
    cfg.column_pruning     = false;
    Optimizer opt(cfg);
    auto result = opt.Optimize(std::move(outer));

    // Should be a single FILTER with AND predicate, whose child is GET
    assert(result->node_type == LogicalPlan::Type::FILTER);
    auto& rf = static_cast<LogicalFilter&>(*result);
    assert(rf.predicate->kind == LogicalExpr::Kind::BINARY_OP);
    auto& and_op = static_cast<LogicalBinaryOp&>(*rf.predicate);
    assert(and_op.op == "AND");
    assert(result->children[0]->node_type == LogicalPlan::Type::GET);

    std::cout << "TestFilterMerge: PASS\n";
}

static void TestTrivialPlanSkipped() {
    // CREATE TABLE plans pass through all optimizer passes unchanged
    auto ct = std::make_unique<LogicalCreateTable>();
    ct->table_name  = "foo";
    ct->schema_name = "main";
    ColumnDefinition c;
    c.name = "x";
    c.type = TypeId::INT64;
    ct->columns.push_back(c);

    Optimizer opt;
    auto result = opt.Optimize(std::move(ct));
    assert(result->node_type == LogicalPlan::Type::CREATE_TABLE);

    std::cout << "TestTrivialPlanSkipped: PASS\n";
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

void RunOptimizerTests() {
    TestConstantFoldArithmetic();
    TestConstantFoldTrueFilter();
    TestPredicatePushdown();
    TestColumnPruning();
    TestFilterMerge();
    TestTrivialPlanSkipped();
    std::cout << "\nAll Optimizer tests passed.\n";
}
