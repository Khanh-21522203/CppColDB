#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <stdexcept>

#include "common/types.hpp"
#include "common/exception.hpp"
#include "catalog/catalog_entry.hpp"
#include "planner/logical_plan/logical_plan.hpp"
#include "planner/physical_planner.hpp"
#include "execution/operator/physical_table_scan.hpp"
#include "execution/operator/physical_filter.hpp"
#include "execution/operator/physical_projection.hpp"
#include "execution/operator/physical_limit.hpp"
#include "execution/operator/physical_create_table.hpp"
#include "execution/operator/physical_drop_table.hpp"

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

static void TestPlanTableScan() {
    // LogicalGet → PhysicalTableScan
    auto get = MakeGet("orders");
    PhysicalPlanner pp;
    auto op = pp.Plan(*get);

    assert(op != nullptr);
    auto* scan = dynamic_cast<PhysicalTableScan*>(op.get());
    assert(scan != nullptr);
    assert(scan->table_name == "orders");
    assert(scan->column_ids == std::vector<size_t>{0});
    assert(scan->output_types[0] == TypeId::INT64);

    std::cout << "TestPlanTableScan: PASS\n";
}

static void TestPlanFilterWrapsChild() {
    // LogicalFilter → LogicalGet → PhysicalFilter with PhysicalTableScan child
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
    filter->children.push_back(MakeGet("orders"));

    PhysicalPlanner pp;
    auto op = pp.Plan(*filter);

    assert(op != nullptr);
    auto* pf = dynamic_cast<PhysicalFilter*>(op.get());
    assert(pf != nullptr);
    assert(pf->predicate != nullptr);
    assert(!pf->children.empty());
    assert(dynamic_cast<PhysicalTableScan*>(pf->children[0].get()) != nullptr);

    std::cout << "TestPlanFilterWrapsChild: PASS\n";
}

static void TestPlanProjectionExprsCopied() {
    auto proj = std::make_unique<LogicalProjection>();

    auto col1 = std::make_unique<BoundColumnRef>();
    col1->column_idx  = 0;
    col1->result_type = TypeId::INT64;

    auto col2 = std::make_unique<BoundColumnRef>();
    col2->column_idx  = 1;
    col2->result_type = TypeId::INT64;

    proj->exprs.push_back(std::move(col1));
    proj->exprs.push_back(std::move(col2));
    proj->output_types = {TypeId::INT64, TypeId::INT64};
    proj->output_names = {"a", "b"};
    proj->children.push_back(MakeGet("orders", 2));

    PhysicalPlanner pp;
    auto op = pp.Plan(*proj);

    auto* pp_op = dynamic_cast<PhysicalProjection*>(op.get());
    assert(pp_op != nullptr);
    assert(pp_op->exprs.size() == 2);

    std::cout << "TestPlanProjectionExprsCopied: PASS\n";
}

static void TestPlanLimitValues() {
    auto lim = std::make_unique<LogicalLimit>();
    lim->limit        = 10;
    lim->offset       = 5;
    lim->output_types = {TypeId::INT64};
    lim->output_names = {"col0"};
    lim->children.push_back(MakeGet("orders"));

    PhysicalPlanner pp;
    auto op = pp.Plan(*lim);

    auto* pl = dynamic_cast<PhysicalLimit*>(op.get());
    assert(pl != nullptr);
    assert(pl->limit  == 10);
    assert(pl->offset == 5);

    std::cout << "TestPlanLimitValues: PASS\n";
}

static void TestPlanCreateTable() {
    auto ct = std::make_unique<LogicalCreateTable>();
    ct->schema_name   = "main";
    ct->table_name    = "t2";
    ct->if_not_exists = false;

    ColumnDefinition c;
    c.name = "x";
    c.type = TypeId::INT64;
    ct->columns.push_back(c);

    PhysicalPlanner pp;
    auto op = pp.Plan(*ct);

    auto* pct = dynamic_cast<PhysicalCreateTable*>(op.get());
    assert(pct != nullptr);
    assert(pct->table_name == "t2");
    assert(pct->columns.size() == 1);

    std::cout << "TestPlanCreateTable: PASS\n";
}

static void TestPlanDropTable() {
    auto dt = std::make_unique<LogicalDropTable>();
    dt->schema_name = "main";
    dt->table_name  = "orders";
    dt->if_exists   = true;

    PhysicalPlanner pp;
    auto op = pp.Plan(*dt);

    auto* pdt = dynamic_cast<PhysicalDropTable*>(op.get());
    assert(pdt != nullptr);
    assert(pdt->if_exists == true);

    std::cout << "TestPlanDropTable: PASS\n";
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

void RunPhysicalPlannerTests() {
    TestPlanTableScan();
    TestPlanFilterWrapsChild();
    TestPlanProjectionExprsCopied();
    TestPlanLimitValues();
    TestPlanCreateTable();
    TestPlanDropTable();
    std::cout << "\nAll PhysicalPlanner tests passed.\n";
}
