#include "execution/operator/physical_table_scan.hpp"
#include "execution/pipeline_executor.hpp"
#include "execution/pipeline.hpp"
#include "execution/physical_result_collector.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction_manager.hpp"
#include "transaction/undo_buffer.hpp"
#include "storage/buffer_manager.hpp"
#include "storage/column/row_group.hpp"
#include "common/types.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <string>

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

    // Insert a DataChunk into table `tname` under a new committed transaction.
    // Returns the RowGroup pointer so callers can call CommitAppend if needed.
    RowGroup* InsertRows(const std::string& tname, const DataChunk& rows) {
        auto tx = txm.BeginTransaction();
        auto* tbl = catalog.GetTable(Catalog::DEFAULT_SCHEMA, tname, *tx);
        std::vector<size_t> col_ids;
        for (size_t i = 0; i < tbl->ColumnCount(); ++i) col_ids.push_back(i);
        RowGroup* rg = tbl->GetOrAddRowGroup();
        size_t rg_id = tbl->row_groups.size() - 1;
        size_t append_start = rg->RowCount(); // offset before append
        rg->Append(rows, col_ids, tx->tx_id);
        tx->undo_buffer.PushInsert(
            InsertUndoEntry{Catalog::DEFAULT_SCHEMA, tname, rg_id,
                            append_start, rows.count});
        txm.Commit(tx);
        // After commit, rg->CommitAppend was called by TransactionManager via
        // ApplyUndoBuffer. If it was not, call it manually with the commit time.
        // tx->commit_time is set during Commit, so we call CommitAppend defensively.
        if (tx->commit_time > 0) {
            rg->CommitAppend(tx->commit_time);
        }
        return rg;
    }

    std::shared_ptr<Transaction> Begin() {
        return txm.BeginTransaction();
    }

    ClientContext MakeContext(Transaction& tx) {
        ClientContext ctx;
        ctx.catalog     = &catalog;
        ctx.transaction = &tx;
        return ctx;
    }
};

// ---------------------------------------------------------------------------
// Build a DataChunk with a single INT64 column from a vector of values.
// ---------------------------------------------------------------------------

static DataChunk MakeInt64Chunk(const std::vector<int64_t>& vals) {
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64});
    chunk.columns[0].Reset(TypeId::INT64, vals.size());
    for (size_t i = 0; i < vals.size(); ++i) {
        chunk.columns[0].int_data[i] = vals[i];
        chunk.columns[0].validity.set(i);
    }
    chunk.columns[0].count = vals.size();
    chunk.count = vals.size();
    return chunk;
}

// Build a DataChunk with multiple columns (all INT64) from rows of values.
static DataChunk MakeMultiColChunk(const std::vector<TypeId>& types,
                                    const std::vector<std::vector<int64_t>>& col_vals) {
    DataChunk chunk;
    chunk.Initialize(types);
    size_t n = col_vals[0].size();
    for (size_t c = 0; c < types.size(); ++c) {
        if (types[c] == TypeId::INT64 || types[c] == TypeId::FLOAT64) {
            chunk.columns[c].Reset(types[c], n);
            for (size_t i = 0; i < n; ++i) {
                if (types[c] == TypeId::INT64) {
                    chunk.columns[c].int_data[i] = col_vals[c][i];
                } else {
                    chunk.columns[c].float_data[i] = static_cast<double>(col_vals[c][i]);
                }
                chunk.columns[c].validity.set(i);
            }
            chunk.columns[c].count = n;
        }
    }
    chunk.count = n;
    return chunk;
}

// ---------------------------------------------------------------------------
// Run a table scan and return the QueryResult.
// ---------------------------------------------------------------------------

static QueryResult RunTableScan(TestDB& db, Transaction& tx,
                                 const std::string& table_name,
                                 const std::vector<size_t>& col_ids,
                                 const std::vector<std::string>& names,
                                 const std::vector<TypeId>& types) {
    PhysicalTableScan scan;
    scan.schema_name = Catalog::DEFAULT_SCHEMA;
    scan.table_name  = table_name;
    scan.column_ids  = col_ids;
    scan.output_types = types;
    scan.output_names = names;

    PhysicalResultCollector collector(names, types);

    Pipeline p;
    p.source = &scan;
    p.sink   = &collector;

    ClientContext ctx = db.MakeContext(tx);
    PipelineExecutor pe(ctx, p);
    pe.Execute();
    return collector.TakeResult();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestTableScanAllRows() {
    TestDB db;
    db.CreateTable("t", {{"id", TypeId::INT64}});
    db.InsertRows("t", MakeInt64Chunk({1, 2, 3, 4, 5}));

    auto tx = db.Begin();
    auto result = RunTableScan(db, *tx, "t", {0}, {"id"}, {TypeId::INT64});
    db.txm.Commit(tx);

    assert(result.success);
    assert(result.RowCount() == 5);
    std::cout << "TestTableScanAllRows: PASS\n";
}

static void TestTableScanColumnPruning() {
    TestDB db;
    db.CreateTable("t2", {{"a", TypeId::INT64},
                           {"b", TypeId::VARCHAR},
                           {"c", TypeId::FLOAT64}});

    // Build a 3-row chunk with INT64 for col 0 and FLOAT64 for col 2.
    // VARCHAR col (col 1) is omitted since we scan col_ids={0,2}.
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64, TypeId::VARCHAR, TypeId::FLOAT64});

    // Col 0: INT64
    chunk.columns[0].Reset(TypeId::INT64, 3);
    for (size_t i = 0; i < 3; ++i) {
        chunk.columns[0].int_data[i] = static_cast<int64_t>(i + 10);
        chunk.columns[0].validity.set(i);
    }
    chunk.columns[0].count = 3;

    // Col 1: VARCHAR
    chunk.columns[1].Reset(TypeId::VARCHAR, 3);
    for (size_t i = 0; i < 3; ++i) {
        chunk.columns[1].str_data[i] = "str";
        chunk.columns[1].validity.set(i);
    }
    chunk.columns[1].count = 3;

    // Col 2: FLOAT64
    chunk.columns[2].Reset(TypeId::FLOAT64, 3);
    for (size_t i = 0; i < 3; ++i) {
        chunk.columns[2].float_data[i] = static_cast<double>(i + 1) * 0.5;
        chunk.columns[2].validity.set(i);
    }
    chunk.columns[2].count = 3;

    chunk.count = 3;

    auto tx_ins = db.Begin();
    auto* tbl = db.catalog.GetTable(Catalog::DEFAULT_SCHEMA, "t2", *tx_ins);
    RowGroup* rg = tbl->GetOrAddRowGroup();
    size_t rg_id = tbl->row_groups.size() - 1;
    size_t t2_append_start = rg->RowCount();
    rg->Append(chunk, {0, 1, 2}, tx_ins->tx_id);
    tx_ins->undo_buffer.PushInsert(
        InsertUndoEntry{Catalog::DEFAULT_SCHEMA, "t2", rg_id, t2_append_start, 3});
    db.txm.Commit(tx_ins);
    if (tx_ins->commit_time > 0) rg->CommitAppend(tx_ins->commit_time);

    // Scan only col 0 (INT64) and col 2 (FLOAT64).
    auto tx = db.Begin();
    auto result = RunTableScan(db, *tx, "t2", {0, 2}, {"a", "c"},
                               {TypeId::INT64, TypeId::FLOAT64});
    db.txm.Commit(tx);

    assert(result.success);
    assert(result.RowCount() == 3);
    assert(result.chunks[0].ColumnCount() == 2);
    assert(result.chunks[0].columns[0].type == TypeId::INT64);
    assert(result.chunks[0].columns[1].type == TypeId::FLOAT64);
    std::cout << "TestTableScanColumnPruning: PASS\n";
}

static void TestTableScanEmptyTable() {
    TestDB db;
    db.CreateTable("empty_t", {{"id", TypeId::INT64}});
    // No rows inserted.

    auto tx = db.Begin();
    auto result = RunTableScan(db, *tx, "empty_t", {0}, {"id"}, {TypeId::INT64});
    db.txm.Commit(tx);

    assert(result.success);
    assert(result.RowCount() == 0);
    std::cout << "TestTableScanEmptyTable: PASS\n";
}

static void TestTableScanMVCC() {
    TestDB db;
    db.CreateTable("mvcc_t", {{"id", TypeId::INT64}});

    // T1 inserts 3 rows but does NOT commit.
    auto t1 = db.Begin();
    auto* tbl = db.catalog.GetTable(Catalog::DEFAULT_SCHEMA, "mvcc_t", *t1);
    RowGroup* rg = tbl->GetOrAddRowGroup();
    size_t rg_id = tbl->row_groups.size() - 1;
    size_t mvcc_append_start = rg->RowCount();
    rg->Append(MakeInt64Chunk({10, 20, 30}), {0}, t1->tx_id);
    t1->undo_buffer.PushInsert(
        InsertUndoEntry{Catalog::DEFAULT_SCHEMA, "mvcc_t", rg_id, mvcc_append_start, 3});

    // T2 starts while T1 is still running (uncommitted).
    auto t2 = db.Begin();
    auto result = RunTableScan(db, *t2, "mvcc_t", {0}, {"id"}, {TypeId::INT64});
    db.txm.Commit(t2);

    // T2 must not see T1's uncommitted rows.
    assert(result.success);
    assert(result.RowCount() == 0);

    // Clean up T1 (rollback — T1 never committed).
    db.txm.Rollback(t1);

    std::cout << "TestTableScanMVCC: PASS\n";
}

static void TestTableScanMultiRowGroups() {
    TestDB db;
    db.CreateTable("big_t", {{"id", TypeId::INT64}});

    // Insert 1024 rows in first batch.
    {
        std::vector<int64_t> vals(1024);
        for (int i = 0; i < 1024; ++i) vals[i] = i;
        db.InsertRows("big_t", MakeInt64Chunk(vals));
    }

    // Insert another 1024 rows in second batch (may go to same or new RowGroup).
    {
        std::vector<int64_t> vals(1024);
        for (int i = 0; i < 1024; ++i) vals[i] = 1024 + i;
        db.InsertRows("big_t", MakeInt64Chunk(vals));
    }

    auto tx = db.Begin();
    auto result = RunTableScan(db, *tx, "big_t", {0}, {"id"}, {TypeId::INT64});
    db.txm.Commit(tx);

    assert(result.success);
    assert(result.RowCount() == 2048);
    std::cout << "TestTableScanMultiRowGroups: PASS\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void RunTableScanTests() {
    TestTableScanAllRows();
    TestTableScanColumnPruning();
    TestTableScanEmptyTable();
    TestTableScanMVCC();
    TestTableScanMultiRowGroups();
    std::cout << "\nAll TableScan tests passed.\n";
}
