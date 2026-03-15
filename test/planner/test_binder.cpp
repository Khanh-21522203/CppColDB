#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <stdexcept>

#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction.hpp"
#include "transaction/transaction_manager.hpp"
#include "transaction/undo_buffer.hpp"
#include "storage/buffer_manager.hpp"
#include "common/types.hpp"
#include "common/exception.hpp"
#include "parser/ast/parsed_statement.hpp"
#include "planner/binder.hpp"
#include "planner/logical_plan/logical_plan.hpp"

using namespace cppcoldb;

// ---------------------------------------------------------------------------
// Shared setup helper
// ---------------------------------------------------------------------------

struct PlannerTestDB {
    BufferManager      bm;
    Catalog            catalog;
    TransactionManager txm;

    PlannerTestDB() : bm(), catalog(bm), txm(catalog) {}

    void CreateTable(const std::string& name,
                     const std::vector<ColumnDefinition>& cols) {
        auto tx = txm.BeginTransaction();
        catalog.CreateTable(Catalog::DEFAULT_SCHEMA, name, cols, *tx);
        tx->undo_buffer.PushCatalogEntry(
            CatalogUndoEntry{Catalog::DEFAULT_SCHEMA, name, true});
        txm.Commit(tx);
    }

    std::shared_ptr<Transaction> ReadTx() {
        return txm.BeginTransaction();
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestBindSimpleSelect() {
    PlannerTestDB db;
    db.CreateTable("orders", {
        {"id",     TypeId::INT64},
        {"amount", TypeId::FLOAT64},
        {"name",   TypeId::VARCHAR},
    });

    SelectStatement stmt;
    stmt.from_table  = "orders";
    stmt.from_schema = "";
    auto col = std::make_unique<ColumnRefExpr>();
    col->column_name = "id";
    stmt.select_list.push_back(std::move(col));

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    auto plan = binder.Bind(stmt);

    assert(plan->node_type == LogicalPlan::Type::PROJECTION);
    auto& proj = static_cast<LogicalProjection&>(*plan);
    assert(proj.exprs.size() == 1);
    assert(proj.exprs[0]->kind == LogicalExpr::Kind::BOUND_COLUMN_REF);
    assert(proj.children[0]->node_type == LogicalPlan::Type::GET);
    db.txm.Commit(tx);

    std::cout << "TestBindSimpleSelect: PASS\n";
}

static void TestBindSelectStar() {
    PlannerTestDB db;
    db.CreateTable("orders", {
        {"id",     TypeId::INT64},
        {"amount", TypeId::FLOAT64},
        {"name",   TypeId::VARCHAR},
    });

    SelectStatement stmt;
    stmt.from_table = "orders";
    stmt.select_list.push_back(std::make_unique<StarExpr>());

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    auto plan = binder.Bind(stmt);

    assert(plan->node_type == LogicalPlan::Type::PROJECTION);
    auto& proj = static_cast<LogicalProjection&>(*plan);
    assert(proj.exprs.size() == 3);
    db.txm.Commit(tx);

    std::cout << "TestBindSelectStar: PASS\n";
}

static void TestBindWhereClause() {
    PlannerTestDB db;
    db.CreateTable("orders", {
        {"id",     TypeId::INT64},
        {"amount", TypeId::FLOAT64},
        {"name",   TypeId::VARCHAR},
    });

    SelectStatement stmt;
    stmt.from_table = "orders";
    auto col = std::make_unique<ColumnRefExpr>();
    col->column_name = "id";
    stmt.select_list.push_back(std::move(col));

    auto where_col = std::make_unique<ColumnRefExpr>();
    where_col->column_name = "amount";
    auto lit = std::make_unique<FloatLitExpr>();
    lit->value = 100.0;
    auto bin = std::make_unique<BinaryOpExpr>();
    bin->op    = ">";
    bin->left  = std::move(where_col);
    bin->right = std::move(lit);
    stmt.where_clause = std::move(bin);

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    auto plan = binder.Bind(stmt);

    assert(plan->node_type == LogicalPlan::Type::PROJECTION);
    assert(plan->children[0]->node_type == LogicalPlan::Type::FILTER);
    assert(plan->children[0]->children[0]->node_type == LogicalPlan::Type::GET);
    db.txm.Commit(tx);

    std::cout << "TestBindWhereClause: PASS\n";
}

static void TestBindTableNotFound() {
    PlannerTestDB db;

    SelectStatement stmt;
    stmt.from_table = "nonexistent";
    stmt.select_list.push_back(std::make_unique<StarExpr>());

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    bool threw = false;
    try { binder.Bind(stmt); } catch (const BindError&) { threw = true; }
    assert(threw);
    db.txm.Commit(tx);

    std::cout << "TestBindTableNotFound: PASS\n";
}

static void TestBindColumnNotFound() {
    PlannerTestDB db;
    db.CreateTable("orders", {
        {"id",     TypeId::INT64},
        {"amount", TypeId::FLOAT64},
        {"name",   TypeId::VARCHAR},
    });

    SelectStatement stmt;
    stmt.from_table = "orders";
    auto col = std::make_unique<ColumnRefExpr>();
    col->column_name = "zzz";
    stmt.select_list.push_back(std::move(col));

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    bool threw = false;
    try { binder.Bind(stmt); } catch (const BindError&) { threw = true; }
    assert(threw);
    db.txm.Commit(tx);

    std::cout << "TestBindColumnNotFound: PASS\n";
}

static void TestBindNumericPromotion() {
    PlannerTestDB db;
    db.CreateTable("orders", {
        {"id",     TypeId::INT64},
        {"amount", TypeId::FLOAT64},
        {"name",   TypeId::VARCHAR},
    });

    SelectStatement stmt;
    stmt.from_table = "orders";
    auto l = std::make_unique<ColumnRefExpr>();
    l->column_name = "id";
    auto r = std::make_unique<ColumnRefExpr>();
    r->column_name = "amount";
    auto bin = std::make_unique<BinaryOpExpr>();
    bin->op    = "+";
    bin->left  = std::move(l);
    bin->right = std::move(r);
    stmt.select_list.push_back(std::move(bin));

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    auto plan = binder.Bind(stmt);

    assert(plan->node_type == LogicalPlan::Type::PROJECTION);
    auto& proj = static_cast<LogicalProjection&>(*plan);
    assert(proj.exprs.size() == 1);
    assert(proj.exprs[0]->result_type == TypeId::FLOAT64);
    db.txm.Commit(tx);

    std::cout << "TestBindNumericPromotion: PASS\n";
}

static void TestBindLimit() {
    PlannerTestDB db;
    db.CreateTable("orders", {
        {"id",     TypeId::INT64},
        {"amount", TypeId::FLOAT64},
        {"name",   TypeId::VARCHAR},
    });

    SelectStatement stmt;
    stmt.from_table = "orders";
    {
        auto c = std::make_unique<ColumnRefExpr>();
        c->column_name = "id";
        stmt.select_list.push_back(std::move(c));
    }
    stmt.limit  = 10;
    stmt.offset = 5;

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    auto plan = binder.Bind(stmt);

    assert(plan->node_type == LogicalPlan::Type::LIMIT);
    auto& lim = static_cast<LogicalLimit&>(*plan);
    assert(lim.limit  == 10);
    assert(lim.offset == 5);
    db.txm.Commit(tx);

    std::cout << "TestBindLimit: PASS\n";
}

static void TestBindCreateTable() {
    PlannerTestDB db;

    CreateTableStatement stmt;
    stmt.schema_name = "";
    stmt.table_name  = "new_table";
    ColumnDef c1;
    c1.name = "x";
    c1.type = TypeId::INT64;
    ColumnDef c2;
    c2.name = "y";
    c2.type = TypeId::VARCHAR;
    stmt.columns = {c1, c2};

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    auto plan = binder.Bind(stmt);

    assert(plan->node_type == LogicalPlan::Type::CREATE_TABLE);
    auto& ct = static_cast<LogicalCreateTable&>(*plan);
    assert(ct.table_name == "new_table");
    assert(ct.columns.size() == 2);
    assert(ct.columns[0].type == TypeId::INT64);
    db.txm.Commit(tx);

    std::cout << "TestBindCreateTable: PASS\n";
}

static void TestBindDropTable() {
    PlannerTestDB db;
    db.CreateTable("orders", {
        {"id",     TypeId::INT64},
        {"amount", TypeId::FLOAT64},
        {"name",   TypeId::VARCHAR},
    });

    DropTableStatement stmt;
    stmt.schema_name = "";
    stmt.table_name  = "orders";
    stmt.if_exists   = true;

    auto tx = db.ReadTx();
    Binder binder(db.catalog, *tx);
    auto plan = binder.Bind(stmt);

    assert(plan->node_type == LogicalPlan::Type::DROP_TABLE);
    auto& dt = static_cast<LogicalDropTable&>(*plan);
    assert(dt.if_exists == true);
    db.txm.Commit(tx);

    std::cout << "TestBindDropTable: PASS\n";
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

void RunBinderTests() {
    TestBindSimpleSelect();
    TestBindSelectStar();
    TestBindWhereClause();
    TestBindTableNotFound();
    TestBindColumnNotFound();
    TestBindNumericPromotion();
    TestBindLimit();
    TestBindCreateTable();
    TestBindDropTable();
    std::cout << "\nAll Binder tests passed.\n";
}
