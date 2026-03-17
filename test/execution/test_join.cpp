#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <stdexcept>
#include <algorithm>

#include "common/types.hpp"
#include "common/exception.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction_manager.hpp"
#include "transaction/undo_buffer.hpp"
#include "storage/buffer_manager.hpp"
#include "storage/column/row_group.hpp"
#include "planner/binder.hpp"
#include "planner/optimizer.hpp"
#include "planner/physical_planner.hpp"
#include "execution/executor.hpp"
#include "execution/physical_result_collector.hpp"
#include "main/client_context.hpp"
#include "parser/tokenizer.hpp"
#include "parser/parser.hpp"

using namespace cppcoldb;

// ---------------------------------------------------------------------------
// TestDB helper
// ---------------------------------------------------------------------------

struct TestDB {
    BufferManager      bm;
    Catalog            catalog;
    TransactionManager txm;

    TestDB() : catalog(bm), txm(catalog) {}

    void CreateTable(const std::string& name,
                     const std::vector<ColumnDefinition>& cols) {
        auto tx = txm.BeginTransaction();
        catalog.CreateTable(Catalog::DEFAULT_SCHEMA, name, cols, *tx);
        tx->undo_buffer.PushCatalogEntry(
            CatalogUndoEntry{Catalog::DEFAULT_SCHEMA, name, true});
        txm.Commit(tx);
    }

    void InsertRows(const std::string& tname, const DataChunk& rows) {
        auto tx = txm.BeginTransaction();
        auto* tbl = catalog.GetTable(Catalog::DEFAULT_SCHEMA, tname, *tx);
        std::vector<size_t> col_ids;
        for (size_t i = 0; i < tbl->ColumnCount(); ++i) col_ids.push_back(i);
        RowGroup* rg = tbl->GetOrAddRowGroup();
        size_t rg_id = tbl->row_groups.size() - 1;
        size_t append_start = rg->RowCount();
        rg->Append(rows, col_ids, tx->tx_id);
        tx->undo_buffer.PushInsert(
            InsertUndoEntry{Catalog::DEFAULT_SCHEMA, tname, rg_id,
                            append_start, rows.count});
        txm.Commit(tx);
        if (tx->commit_time > 0) rg->CommitAppend(tx->commit_time);
    }

    QueryResult Run(const std::string& sql) {
        auto tx = txm.BeginTransaction();
        ClientContext ctx;
        ctx.catalog     = &catalog;
        ctx.transaction = tx.get();

        Tokenizer tok(sql);
        auto tokens = tok.Tokenize();
        Parser parser(std::move(tokens));
        auto stmt = parser.Parse();

        Binder binder(catalog, *tx);
        auto logical = binder.Bind(*stmt);

        Optimizer opt;
        logical = opt.Optimize(std::move(logical));

        PhysicalPlanner pp;
        auto physical = pp.Plan(*logical);

        Executor exec(ctx);
        exec.Initialize(std::move(physical));
        exec.Execute();

        txm.Commit(tx);
        return exec.GetResult();
    }
};

// ---------------------------------------------------------------------------
// Data helpers
// ---------------------------------------------------------------------------

static DataChunk MakeInt64Int64Chunk(const std::vector<int64_t>& a,
                                     const std::vector<int64_t>& b) {
    assert(a.size() == b.size());
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64, TypeId::INT64});
    chunk.columns[0].Reset(TypeId::INT64, a.size());
    chunk.columns[1].Reset(TypeId::INT64, b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        chunk.columns[0].int_data[i] = a[i];
        chunk.columns[0].validity.set(i);
        chunk.columns[1].int_data[i] = b[i];
        chunk.columns[1].validity.set(i);
    }
    chunk.count = a.size();
    return chunk;
}

static DataChunk MakeInt64VarcharChunk(const std::vector<int64_t>& ids,
                                       const std::vector<std::string>& names) {
    assert(ids.size() == names.size());
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64, TypeId::VARCHAR});
    chunk.columns[0].Reset(TypeId::INT64,   ids.size());
    chunk.columns[1].Reset(TypeId::VARCHAR, names.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        chunk.columns[0].int_data[i] = ids[i];
        chunk.columns[0].validity.set(i);
        chunk.columns[1].str_data[i] = names[i];
        chunk.columns[1].validity.set(i);
    }
    chunk.count = ids.size();
    return chunk;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Basic INNER JOIN: employees join departments
static void TestJoinBasicInnerJoin() {
    TestDB db;

    // emp(id INT64, dept_id INT64)
    db.CreateTable("emp", {
        {"id",      TypeId::INT64},
        {"dept_id", TypeId::INT64}
    });
    // dept(id INT64, name VARCHAR)
    db.CreateTable("dept", {
        {"id",   TypeId::INT64},
        {"name", TypeId::VARCHAR}
    });

    // emp rows: (1,10),(2,20),(3,10)
    auto emp_chunk = MakeInt64Int64Chunk({1,2,3}, {10,20,10});
    db.InsertRows("emp", emp_chunk);

    // dept rows: (10,"Engineering"),(20,"Marketing")
    auto dept_chunk = MakeInt64VarcharChunk({10,20}, {"Engineering","Marketing"});
    db.InsertRows("dept", dept_chunk);

    auto result = db.Run(
        "SELECT emp.id, dept.name FROM emp INNER JOIN dept ON emp.dept_id = dept.id");

    assert(result.chunks.size() >= 1);
    size_t total = 0;
    for (const auto& ch : result.chunks) total += ch.count;
    assert(total == 3); // 3 employees all have matching departments

    // Collect (emp.id, dept.name) pairs
    std::vector<std::pair<int64_t,std::string>> rows;
    for (const auto& ch : result.chunks) {
        for (size_t i = 0; i < ch.count; ++i) {
            rows.push_back({ch.columns[0].int_data[i], ch.columns[1].str_data[i]});
        }
    }
    std::sort(rows.begin(), rows.end());
    assert(rows[0].first == 1 && rows[0].second == "Engineering");
    assert(rows[1].first == 2 && rows[1].second == "Marketing");
    assert(rows[2].first == 3 && rows[2].second == "Engineering");

    std::cout << "TestJoinBasicInnerJoin PASSED\n";
}

// JOIN with no matching rows → empty result
static void TestJoinNoMatches() {
    TestDB db;

    db.CreateTable("left_t", {{"id", TypeId::INT64}, {"jkey", TypeId::INT64}});
    db.CreateTable("right_t", {{"jkey", TypeId::INT64}, {"val", TypeId::INT64}});

    auto left_chunk = MakeInt64Int64Chunk({1,2,3}, {100,200,300});
    db.InsertRows("left_t", left_chunk);

    // right table has keys 400,500 — no overlap with left
    auto right_chunk = MakeInt64Int64Chunk({400,500}, {40,50});
    db.InsertRows("right_t", right_chunk);

    auto result = db.Run(
        "SELECT left_t.id FROM left_t INNER JOIN right_t ON left_t.jkey = right_t.jkey");

    size_t total = 0;
    for (const auto& ch : result.chunks) total += ch.count;
    assert(total == 0);

    std::cout << "TestJoinNoMatches PASSED\n";
}

// One-to-many: single right row matches multiple left rows
static void TestJoinOneToMany() {
    TestDB db;

    db.CreateTable("orders", {{"id", TypeId::INT64}, {"cid", TypeId::INT64}});
    db.CreateTable("customers", {{"id", TypeId::INT64}, {"name", TypeId::VARCHAR}});

    // 3 orders for customer 1, 1 order for customer 2
    auto ord_chunk = MakeInt64Int64Chunk({10,20,30,40}, {1,1,1,2});
    db.InsertRows("orders", ord_chunk);

    auto cust_chunk = MakeInt64VarcharChunk({1,2}, {"Alice","Bob"});
    db.InsertRows("customers", cust_chunk);

    auto result = db.Run(
        "SELECT orders.id, customers.name FROM orders INNER JOIN customers "
        "ON orders.cid = customers.id");

    size_t total = 0;
    for (const auto& ch : result.chunks) total += ch.count;
    assert(total == 4);

    // Count how many times "Alice" appears
    size_t alice_count = 0;
    for (const auto& ch : result.chunks) {
        for (size_t i = 0; i < ch.count; ++i) {
            if (ch.columns[1].str_data[i] == "Alice") ++alice_count;
        }
    }
    assert(alice_count == 3);

    std::cout << "TestJoinOneToMany PASSED\n";
}

// JOIN + WHERE filter
static void TestJoinWithWhereFilter() {
    TestDB db;

    db.CreateTable("emp", {{"id", TypeId::INT64}, {"dept_id", TypeId::INT64}});
    db.CreateTable("dept", {{"id", TypeId::INT64}, {"name", TypeId::VARCHAR}});

    auto emp_chunk = MakeInt64Int64Chunk({1,2,3,4}, {10,20,10,20});
    db.InsertRows("emp", emp_chunk);

    auto dept_chunk = MakeInt64VarcharChunk({10,20}, {"Eng","Mkt"});
    db.InsertRows("dept", dept_chunk);

    // Only employees with dept_id=10 (Engineering)
    auto result = db.Run(
        "SELECT emp.id FROM emp INNER JOIN dept ON emp.dept_id = dept.id "
        "WHERE dept.id = 10");

    size_t total = 0;
    for (const auto& ch : result.chunks) total += ch.count;
    assert(total == 2); // emp 1 and 3

    std::vector<int64_t> ids;
    for (const auto& ch : result.chunks)
        for (size_t i = 0; i < ch.count; ++i)
            ids.push_back(ch.columns[0].int_data[i]);
    std::sort(ids.begin(), ids.end());
    assert(ids == (std::vector<int64_t>{1, 3}));

    std::cout << "TestJoinWithWhereFilter PASSED\n";
}

// SELECT * from JOIN produces all columns from both tables
static void TestJoinSelectStar() {
    TestDB db;

    db.CreateTable("a", {{"x", TypeId::INT64}, {"y", TypeId::INT64}});
    db.CreateTable("b", {{"p", TypeId::INT64}, {"q", TypeId::INT64}});

    auto a_chunk = MakeInt64Int64Chunk({1,2}, {10,20});
    db.InsertRows("a", a_chunk);

    auto b_chunk = MakeInt64Int64Chunk({1,2}, {100,200});
    db.InsertRows("b", b_chunk);

    auto result = db.Run("SELECT * FROM a INNER JOIN b ON a.x = b.p");

    size_t total = 0;
    for (const auto& ch : result.chunks) total += ch.count;
    assert(total == 2);

    // Should have 4 columns: a.x, a.y, b.p, b.q
    assert(result.chunks[0].columns.size() == 4);

    std::cout << "TestJoinSelectStar PASSED\n";
}

// JOIN where right table is empty → empty result
static void TestJoinEmptyBuildSide() {
    TestDB db;

    db.CreateTable("left_t",  {{"id", TypeId::INT64}});
    db.CreateTable("right_t", {{"id", TypeId::INT64}});

    // left has 3 rows, right is empty
    DataChunk left_chunk;
    left_chunk.Initialize({TypeId::INT64});
    left_chunk.columns[0].Reset(TypeId::INT64, 3);
    for (size_t i = 0; i < 3; ++i) {
        left_chunk.columns[0].int_data[i] = (int64_t)(i + 1);
        left_chunk.columns[0].validity.set(i);
    }
    left_chunk.count = 3;
    db.InsertRows("left_t", left_chunk);
    // right_t has no rows inserted

    auto result = db.Run(
        "SELECT left_t.id FROM left_t INNER JOIN right_t ON left_t.id = right_t.id");

    size_t total = 0;
    for (const auto& ch : result.chunks) total += ch.count;
    assert(total == 0);

    std::cout << "TestJoinEmptyBuildSide PASSED\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void RunJoinTests() {
    TestJoinBasicInnerJoin();
    TestJoinNoMatches();
    TestJoinOneToMany();
    TestJoinWithWhereFilter();
    TestJoinSelectStar();
    TestJoinEmptyBuildSide();
    std::cout << "All join tests PASSED\n";
}
