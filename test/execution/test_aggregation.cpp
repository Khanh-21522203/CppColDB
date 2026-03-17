#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <stdexcept>
#include <cmath>

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
// TestDB helper (same pattern as test_table_scan.cpp)
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

    // Run a SELECT query and return QueryResult.
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
// Helpers to build DataChunks
// ---------------------------------------------------------------------------

static DataChunk MakeInt64Chunk(const std::vector<int64_t>& vals) {
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64});
    chunk.columns[0].Reset(TypeId::INT64, vals.size());
    for (size_t i = 0; i < vals.size(); ++i) {
        chunk.columns[0].int_data[i] = vals[i];
        chunk.columns[0].validity.set(i);
    }
    chunk.count = vals.size();
    return chunk;
}

static DataChunk MakeVarcharInt64Chunk(const std::vector<std::string>& sv,
                                        const std::vector<int64_t>& iv) {
    DataChunk chunk;
    chunk.Initialize({TypeId::VARCHAR, TypeId::INT64});
    chunk.columns[0].Reset(TypeId::VARCHAR, sv.size());
    chunk.columns[1].Reset(TypeId::INT64,   iv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        chunk.columns[0].str_data[i] = sv[i];
        chunk.columns[0].validity.set(i);
        chunk.columns[1].int_data[i] = iv[i];
        chunk.columns[1].validity.set(i);
    }
    chunk.count = sv.size();
    return chunk;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestAggCountStar() {
    TestDB db;
    db.CreateTable("t", {{"id", TypeId::INT64}});
    auto chunk = MakeInt64Chunk({1, 2, 3, 4, 5});
    db.InsertRows("t", chunk);

    auto result = db.Run("SELECT COUNT(*) FROM t");

    assert(result.success);
    assert(result.RowCount() == 1);
    assert(result.chunks[0].columns[0].int_data[0] == 5);
    std::cout << "TestAggCountStar: PASS\n";
}

static void TestAggSum() {
    TestDB db;
    db.CreateTable("t", {{"val", TypeId::INT64}});
    auto chunk = MakeInt64Chunk({10, 20, 30, 40});
    db.InsertRows("t", chunk);

    auto result = db.Run("SELECT SUM(val) FROM t");

    assert(result.success);
    assert(result.RowCount() == 1);
    // SUM returns INT64 for INT64 input
    assert(result.chunks[0].columns[0].int_data[0] == 100);
    std::cout << "TestAggSum: PASS\n";
}

static void TestAggAvg() {
    TestDB db;
    db.CreateTable("t", {{"val", TypeId::INT64}});
    auto chunk = MakeInt64Chunk({2, 4, 6, 8});
    db.InsertRows("t", chunk);

    auto result = db.Run("SELECT AVG(val) FROM t");

    assert(result.success);
    assert(result.RowCount() == 1);
    double avg = result.chunks[0].columns[0].float_data[0];
    assert(std::abs(avg - 5.0) < 1e-9);
    std::cout << "TestAggAvg: PASS\n";
}

static void TestAggMin() {
    TestDB db;
    db.CreateTable("t", {{"val", TypeId::INT64}});
    auto chunk = MakeInt64Chunk({7, 3, 9, 1, 5});
    db.InsertRows("t", chunk);

    auto result = db.Run("SELECT MIN(val) FROM t");

    assert(result.success);
    assert(result.RowCount() == 1);
    assert(result.chunks[0].columns[0].int_data[0] == 1);
    std::cout << "TestAggMin: PASS\n";
}

static void TestAggMax() {
    TestDB db;
    db.CreateTable("t", {{"val", TypeId::INT64}});
    auto chunk = MakeInt64Chunk({7, 3, 9, 1, 5});
    db.InsertRows("t", chunk);

    auto result = db.Run("SELECT MAX(val) FROM t");

    assert(result.success);
    assert(result.RowCount() == 1);
    assert(result.chunks[0].columns[0].int_data[0] == 9);
    std::cout << "TestAggMax: PASS\n";
}

static void TestAggGroupBy() {
    TestDB db;
    db.CreateTable("emp", {{"dept", TypeId::VARCHAR}, {"salary", TypeId::INT64}});
    auto chunk = MakeVarcharInt64Chunk(
        {"eng", "eng", "hr", "eng", "hr"},
        {100,   200,   50,  300,   75});
    db.InsertRows("emp", chunk);

    auto result = db.Run("SELECT dept, COUNT(*) FROM emp GROUP BY dept");

    assert(result.success);
    assert(result.RowCount() == 2);

    // Two groups: eng (3 rows) and hr (2 rows) — order may vary.
    bool found_eng = false, found_hr = false;
    for (size_t i = 0; i < result.RowCount(); ++i) {
        // row is split across chunks; get from chunk 0 since we have < 1024 rows
        const DataChunk& ch = result.chunks[0];
        std::string dept = ch.columns[0].str_data[i];
        int64_t cnt      = ch.columns[1].int_data[i];
        if (dept == "eng") { assert(cnt == 3); found_eng = true; }
        if (dept == "hr")  { assert(cnt == 2); found_hr  = true; }
    }
    assert(found_eng && found_hr);
    std::cout << "TestAggGroupBy: PASS\n";
}

static void TestAggGroupBySumMultiAgg() {
    TestDB db;
    db.CreateTable("emp", {{"dept", TypeId::VARCHAR}, {"salary", TypeId::INT64}});
    auto chunk = MakeVarcharInt64Chunk(
        {"eng", "eng", "hr"},
        {100,   200,   50});
    db.InsertRows("emp", chunk);

    auto result = db.Run("SELECT dept, SUM(salary), COUNT(*) FROM emp GROUP BY dept");

    assert(result.success);
    assert(result.RowCount() == 2);

    bool found_eng = false, found_hr = false;
    for (size_t i = 0; i < result.RowCount(); ++i) {
        const DataChunk& ch = result.chunks[0];
        std::string dept = ch.columns[0].str_data[i];
        int64_t sum_sal  = ch.columns[1].int_data[i];
        int64_t cnt      = ch.columns[2].int_data[i];
        if (dept == "eng") { assert(sum_sal == 300); assert(cnt == 2); found_eng = true; }
        if (dept == "hr")  { assert(sum_sal == 50);  assert(cnt == 1); found_hr  = true; }
    }
    assert(found_eng && found_hr);
    std::cout << "TestAggGroupBySumMultiAgg: PASS\n";
}

static void TestAggUngroupedOnEmpty() {
    TestDB db;
    db.CreateTable("t", {{"val", TypeId::INT64}});
    // No inserts — empty table.

    auto result = db.Run("SELECT COUNT(*) FROM t");

    assert(result.success);
    assert(result.RowCount() == 1);
    assert(result.chunks[0].columns[0].int_data[0] == 0);
    std::cout << "TestAggUngroupedOnEmpty: PASS\n";
}

static void TestAggHashTableResize() {
    // Insert many distinct groups to force resize (initial capacity = 256).
    TestDB db;
    db.CreateTable("t", {{"grp", TypeId::INT64}, {"val", TypeId::INT64}});

    // 300 distinct groups.
    const size_t N = 300;
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64, TypeId::INT64});
    chunk.columns[0].Reset(TypeId::INT64, N);
    chunk.columns[1].Reset(TypeId::INT64, N);
    int64_t total = 0;
    for (size_t i = 0; i < N; ++i) {
        chunk.columns[0].int_data[i] = static_cast<int64_t>(i);
        chunk.columns[0].validity.set(i);
        chunk.columns[1].int_data[i] = 1;
        chunk.columns[1].validity.set(i);
        total += 1;
    }
    chunk.count = N;
    db.InsertRows("t", chunk);

    auto result = db.Run("SELECT COUNT(*) FROM t GROUP BY grp");

    assert(result.success);
    assert(result.RowCount() == N);
    // Each group has exactly 1 row.
    size_t rows_checked = 0;
    for (const auto& ch : result.chunks) {
        for (size_t i = 0; i < ch.count; ++i) {
            assert(ch.columns[0].int_data[i] == 1);
            ++rows_checked;
        }
    }
    assert(rows_checked == N);
    std::cout << "TestAggHashTableResize: PASS\n";
}

static void TestAggOutputChunkBoundary() {
    // More than 1024 groups → output spans multiple chunks.
    TestDB db;
    db.CreateTable("t", {{"grp", TypeId::INT64}});

    // 1200 distinct groups — need two INSERT chunks since <= 1024 per chunk.
    const size_t N = 1200;
    // Insert in two batches.
    {
        DataChunk ch1;
        ch1.Initialize({TypeId::INT64});
        size_t batch = 1024;
        ch1.columns[0].Reset(TypeId::INT64, batch);
        for (size_t i = 0; i < batch; ++i) {
            ch1.columns[0].int_data[i] = static_cast<int64_t>(i);
            ch1.columns[0].validity.set(i);
        }
        ch1.count = batch;
        db.InsertRows("t", ch1);
    }
    {
        DataChunk ch2;
        ch2.Initialize({TypeId::INT64});
        size_t batch = N - 1024;
        ch2.columns[0].Reset(TypeId::INT64, batch);
        for (size_t i = 0; i < batch; ++i) {
            ch2.columns[0].int_data[i] = static_cast<int64_t>(1024 + i);
            ch2.columns[0].validity.set(i);
        }
        ch2.count = batch;
        db.InsertRows("t", ch2);
    }

    auto result = db.Run("SELECT grp, COUNT(*) FROM t GROUP BY grp");

    assert(result.success);
    assert(result.RowCount() == N);
    std::cout << "TestAggOutputChunkBoundary: PASS\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void RunAggregationTests() {
    std::cout << "\n=== Aggregation Tests ===\n";
    TestAggCountStar();
    TestAggSum();
    TestAggAvg();
    TestAggMin();
    TestAggMax();
    TestAggGroupBy();
    TestAggGroupBySumMultiAgg();
    TestAggUngroupedOnEmpty();
    TestAggHashTableResize();
    TestAggOutputChunkBoundary();
    std::cout << "All Aggregation tests passed.\n";
}
