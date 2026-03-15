#include "transaction/transaction_manager.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"
#include "storage/buffer_manager.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace cppcoldb;

// ---- Helpers ---------------------------------------------------------------

struct MVCCEnv {
    BufferManager      bm;
    Catalog            cat;
    TransactionManager tm;

    MVCCEnv() : cat(bm), tm(cat) {}

    // Create a committed table "main.t1" with a single INT64 column.
    void CreateTable(const std::string& table_name = "t1") {
        auto setup_tx = tm.BeginTransaction();
        cat.CreateTable("main", table_name, {{"val", TypeId::INT64}}, *setup_tx);
        setup_tx->undo_buffer.PushCatalogEntry(
            CatalogUndoEntry{"main", table_name, true});
        tm.Commit(setup_tx);
    }
};

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

static size_t ScanAllRows(Catalog& cat, const std::string& table,
                           const Transaction& tx) {
    auto* tbl = cat.GetTable("main", table, tx);
    if (!tbl) return 0;
    size_t total = 0;
    for (auto& rg_ptr : tbl->row_groups) {
        size_t offset = 0;
        while (offset < rg_ptr->RowCount()) {
            DataChunk out;
            size_t n = rg_ptr->Scan(offset, {0}, out, tx);
            if (n == 0) break;
            total += n;
        }
    }
    return total;
}

// ---- Tests -----------------------------------------------------------------

static void TestExplicitBeginCommit() {
    MVCCEnv env;
    env.CreateTable();

    auto tx = env.tm.BeginTransaction();
    auto* tbl = env.cat.GetTable("main", "t1", *tx);
    assert(tbl != nullptr);

    RowGroup* rg = tbl->GetOrAddRowGroup();
    size_t rg_id = tbl->row_groups.size() - 1;
    auto chunk = MakeInt64Chunk({10, 20, 30});
    rg->Append(chunk, {0}, tx->tx_id);
    tx->undo_buffer.PushInsert(
        InsertUndoEntry{"main", "t1", rg_id, 0, 3});

    env.tm.Commit(tx);

    // A new transaction starting after commit should see 3 rows.
    auto reader = env.tm.BeginTransaction();
    size_t count = ScanAllRows(env.cat, "t1", *reader);
    assert(count == 3);
    env.tm.Commit(reader);
    std::cout << "TestExplicitBeginCommit: PASS\n";
}

static void TestExplicitBeginRollback() {
    MVCCEnv env;
    env.CreateTable();

    auto tx = env.tm.BeginTransaction();
    auto* tbl = env.cat.GetTable("main", "t1", *tx);
    RowGroup* rg = tbl->GetOrAddRowGroup();
    size_t rg_id = tbl->row_groups.size() - 1;
    auto chunk = MakeInt64Chunk({1, 2, 3, 4, 5});
    rg->Append(chunk, {0}, tx->tx_id);
    tx->undo_buffer.PushInsert(
        InsertUndoEntry{"main", "t1", rg_id, 0, 5});

    env.tm.Rollback(tx);

    // After rollback, rows are gone.
    auto reader = env.tm.BeginTransaction();
    size_t count = ScanAllRows(env.cat, "t1", *reader);
    assert(count == 0);
    env.tm.Commit(reader);
    std::cout << "TestExplicitBeginRollback: PASS\n";
}

static void TestMVCCSnapshotIsolation() {
    // T2 starts before T1 commits → T2 cannot see T1's rows.
    MVCCEnv env;
    env.CreateTable();

    auto t1 = env.tm.BeginTransaction();
    auto t2 = env.tm.BeginTransaction(); // starts before T1 commits

    auto* tbl = env.cat.GetTable("main", "t1", *t1);
    RowGroup* rg = tbl->GetOrAddRowGroup();
    size_t rg_id = tbl->row_groups.size() - 1;
    rg->Append(MakeInt64Chunk({100, 200}), {0}, t1->tx_id);
    t1->undo_buffer.PushInsert(InsertUndoEntry{"main", "t1", rg_id, 0, 2});

    env.tm.Commit(t1);

    // T2 started before T1 committed; T2 should see 0 rows.
    size_t count = ScanAllRows(env.cat, "t1", *t2);
    assert(count == 0);
    env.tm.Commit(t2);
    std::cout << "TestMVCCSnapshotIsolation: PASS\n";
}

static void TestMVCCSnapshotAfterCommit() {
    // T3 starts after T1 commits → T3 sees T1's rows.
    MVCCEnv env;
    env.CreateTable();

    auto t1 = env.tm.BeginTransaction();
    auto* tbl = env.cat.GetTable("main", "t1", *t1);
    RowGroup* rg = tbl->GetOrAddRowGroup();
    size_t rg_id = tbl->row_groups.size() - 1;
    rg->Append(MakeInt64Chunk({100, 200, 300}), {0}, t1->tx_id);
    t1->undo_buffer.PushInsert(InsertUndoEntry{"main", "t1", rg_id, 0, 3});
    env.tm.Commit(t1);

    // T3 starts after T1 committed.
    auto t3 = env.tm.BeginTransaction();
    size_t count = ScanAllRows(env.cat, "t1", *t3);
    assert(count == 3);
    env.tm.Commit(t3);
    std::cout << "TestMVCCSnapshotAfterCommit: PASS\n";
}

static void TestCommitCreateTable() {
    // CREATE TABLE inside a transaction; only visible after commit.
    BufferManager bm;
    Catalog cat(bm);
    TransactionManager tm(cat);

    auto t1 = tm.BeginTransaction();
    auto t2 = tm.BeginTransaction(); // starts concurrently with T1

    cat.CreateTable("main", "new_table", {{"x", TypeId::INT64}}, *t1);
    t1->undo_buffer.PushCatalogEntry(CatalogUndoEntry{"main", "new_table", true});

    // T2 cannot see T1's uncommitted table.
    assert(cat.GetTable("main", "new_table", *t2) == nullptr);

    tm.Commit(t1);

    // T2 still can't see it (started before T1 committed).
    assert(cat.GetTable("main", "new_table", *t2) == nullptr);

    // A new T3 after commit sees it.
    auto t3 = tm.BeginTransaction();
    assert(cat.GetTable("main", "new_table", *t3) != nullptr);
    tm.Commit(t3);

    tm.Rollback(t2);
    std::cout << "TestCommitCreateTable: PASS\n";
}

static void TestRollbackCreateTable() {
    BufferManager bm;
    Catalog cat(bm);
    TransactionManager tm(cat);

    auto t1 = tm.BeginTransaction();
    cat.CreateTable("main", "rollback_table", {{"x", TypeId::INT64}}, *t1);
    t1->undo_buffer.PushCatalogEntry(CatalogUndoEntry{"main", "rollback_table", true});
    tm.Rollback(t1);

    auto reader = tm.BeginTransaction();
    assert(cat.GetTable("main", "rollback_table", *reader) == nullptr);
    tm.Commit(reader);
    std::cout << "TestRollbackCreateTable: PASS\n";
}

static void TestMVCCDeleteCommit() {
    // T1 deletes row 1; after T1 commits, T3 (started after) cannot see row 1.
    MVCCEnv env;
    env.CreateTable();

    // Pre-populate table with 3 rows (committed).
    auto setup = env.tm.BeginTransaction();
    auto* tbl = env.cat.GetTable("main", "t1", *setup);
    RowGroup* rg = tbl->GetOrAddRowGroup();
    size_t rg_id = tbl->row_groups.size() - 1;
    rg->Append(MakeInt64Chunk({10, 20, 30}), {0});  // committed (no tx_id)
    setup->undo_buffer.PushInsert(InsertUndoEntry{"main", "t1", rg_id, 0, 3});
    env.tm.Commit(setup);

    // T1 deletes row 1 (row_offset=1 in rg 0).
    auto t1 = env.tm.BeginTransaction();
    row_t row_id = MakeRowId(0, 1);
    rg->GetVersionInfo().MarkDeleted(1, t1->tx_id);
    t1->undo_buffer.PushDelete(DeleteUndoEntry{"main", "t1", {row_id}});
    env.tm.Commit(t1);

    // T3 starts after T1 committed: sees only 2 rows.
    auto t3 = env.tm.BeginTransaction();
    size_t count = ScanAllRows(env.cat, "t1", *t3);
    assert(count == 2);
    env.tm.Commit(t3);
    std::cout << "TestMVCCDeleteCommit: PASS\n";
}

static void TestMVCCDeleteRollback() {
    // T1 deletes row, then rolls back; row still visible.
    MVCCEnv env;
    env.CreateTable();

    auto setup = env.tm.BeginTransaction();
    auto* tbl = env.cat.GetTable("main", "t1", *setup);
    RowGroup* rg = tbl->GetOrAddRowGroup();
    size_t rg_id = tbl->row_groups.size() - 1;
    rg->Append(MakeInt64Chunk({1, 2, 3}), {0});
    setup->undo_buffer.PushInsert(InsertUndoEntry{"main", "t1", rg_id, 0, 3});
    env.tm.Commit(setup);

    // T1 deletes row 0, then rolls back.
    auto t1 = env.tm.BeginTransaction();
    row_t row_id = MakeRowId(0, 0);
    rg->GetVersionInfo().MarkDeleted(0, t1->tx_id);
    t1->undo_buffer.PushDelete(DeleteUndoEntry{"main", "t1", {row_id}});
    env.tm.Rollback(t1);

    // All 3 rows are visible again.
    auto reader = env.tm.BeginTransaction();
    size_t count = ScanAllRows(env.cat, "t1", *reader);
    assert(count == 3);
    env.tm.Commit(reader);
    std::cout << "TestMVCCDeleteRollback: PASS\n";
}

void RunMVCCTests() {
    TestExplicitBeginCommit();
    TestExplicitBeginRollback();
    TestMVCCSnapshotIsolation();
    TestMVCCSnapshotAfterCommit();
    TestCommitCreateTable();
    TestRollbackCreateTable();
    TestMVCCDeleteCommit();
    TestMVCCDeleteRollback();
    std::cout << "\nAll MVCC tests passed.\n";
}
