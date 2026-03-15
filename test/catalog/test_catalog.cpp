#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction.hpp"
#include "storage/buffer_manager.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace cppcoldb;

static std::vector<ColumnDefinition> MakeCols() {
    return {{"id", TypeId::INT64}, {"name", TypeId::VARCHAR}};
}

static Transaction MakeTx(TransactionId id, timestamp_t start) {
    Transaction t;
    t.tx_id      = id;
    t.start_time = start;
    return t;
}

// ---- Tests -----------------------------------------------------------------

static void TestCatalogCreateTable() {
    BufferManager bm;
    Catalog cat(bm);
    Transaction tx = MakeTx(1, 0);
    cat.CreateTable("main", "orders", MakeCols(), tx);
    cat.CommitEntry("main", "orders", 1, 5);

    Transaction reader = MakeTx(2, 10);
    auto* entry = cat.GetTable("main", "orders", reader);
    assert(entry != nullptr);
    assert(entry->name == "orders");
    assert(entry->ColumnCount() == 2);
    assert(entry->FindColumn("id") == 0);
    assert(entry->FindColumn("name") == 1);
    assert(entry->FindColumn("missing") == -1);
    std::cout << "TestCatalogCreateTable: PASS\n";
}

static void TestCatalogTableNotFound() {
    BufferManager bm;
    Catalog cat(bm);
    Transaction tx = MakeTx(1, 0);
    bool threw = false;
    try { cat.GetEntry("main", "nonexistent", tx); }
    catch (const BindError&) { threw = true; }
    assert(threw);
    std::cout << "TestCatalogTableNotFound: PASS\n";
}

static void TestCatalogSchemaNotFound() {
    BufferManager bm;
    Catalog cat(bm);
    Transaction tx = MakeTx(1, 0);
    bool threw = false;
    try { cat.GetEntry("unknown_schema", "t", tx); }
    catch (const BindError&) { threw = true; }
    assert(threw);
    std::cout << "TestCatalogSchemaNotFound: PASS\n";
}

static void TestCatalogMVCCCreateVisibility() {
    // T1 creates table (uncommitted). T2 started before T1 commits → T2 cannot see table.
    BufferManager bm;
    Catalog cat(bm);

    // T2 starts before T1's commit time.
    Transaction t1 = MakeTx(1, 1);
    Transaction t2 = MakeTx(2, 1); // same start_time as T1

    cat.CreateTable("main", "t", MakeCols(), t1);
    // T1 not yet committed — T2 cannot see it.
    auto* e = cat.GetTable("main", "t", t2);
    assert(e == nullptr);

    // T1 commits at time 5. T2's start_time=1 < 5 → T2 still can't see it.
    cat.CommitEntry("main", "t", t1.tx_id, 5);
    e = cat.GetTable("main", "t", t2);
    assert(e == nullptr);

    // T3 starts at time 10 (after T1 committed at 5) → T3 sees the table.
    Transaction t3 = MakeTx(3, 10);
    e = cat.GetTable("main", "t", t3);
    assert(e != nullptr);

    std::cout << "TestCatalogMVCCCreateVisibility: PASS\n";
}

static void TestCatalogDropTable() {
    BufferManager bm;
    Catalog cat(bm);

    Transaction t1 = MakeTx(1, 1);
    cat.CreateTable("main", "t", MakeCols(), t1);
    cat.CommitEntry("main", "t", t1.tx_id, 2);

    // T2 drops the table.
    Transaction t2 = MakeTx(2, 5);
    cat.DropTable("main", "t", t2);
    cat.CommitEntry("main", "t", t2.tx_id, 6);

    // T3 starts after the drop committed → cannot see table.
    Transaction t3 = MakeTx(3, 10);
    auto* e = cat.GetTable("main", "t", t3);
    assert(e == nullptr);

    std::cout << "TestCatalogDropTable: PASS\n";
}

static void TestCatalogMVCCDropVisibility() {
    // T2 drops table; T3 started before T2 commits → T3 still sees the table.
    BufferManager bm;
    Catalog cat(bm);

    Transaction t1 = MakeTx(1, 1);
    cat.CreateTable("main", "t", MakeCols(), t1);
    cat.CommitEntry("main", "t", t1.tx_id, 2);

    Transaction t2 = MakeTx(2, 5);
    cat.DropTable("main", "t", t2);

    // T3 starts before T2 commits; drop is uncommitted → T3 sees the table.
    Transaction t3 = MakeTx(3, 5);
    auto* e = cat.GetTable("main", "t", t3);
    assert(e != nullptr);

    std::cout << "TestCatalogMVCCDropVisibility: PASS\n";
}

static void TestCatalogRollbackCreate() {
    BufferManager bm;
    Catalog cat(bm);
    Transaction t1 = MakeTx(1, 1);
    cat.CreateTable("main", "t", MakeCols(), t1);
    cat.RollbackCreate("main", "t", t1.tx_id);

    Transaction reader = MakeTx(2, 10);
    auto* e = cat.GetTable("main", "t", reader);
    assert(e == nullptr);
    std::cout << "TestCatalogRollbackCreate: PASS\n";
}

static void TestCatalogRollbackDrop() {
    BufferManager bm;
    Catalog cat(bm);

    Transaction t1 = MakeTx(1, 1);
    cat.CreateTable("main", "t", MakeCols(), t1);
    cat.CommitEntry("main", "t", t1.tx_id, 2);

    Transaction t2 = MakeTx(2, 5);
    cat.DropTable("main", "t", t2);
    cat.RollbackDrop("main", "t", t2.tx_id);

    Transaction reader = MakeTx(3, 10);
    auto* e = cat.GetTable("main", "t", reader);
    assert(e != nullptr); // still visible after rollback of drop
    std::cout << "TestCatalogRollbackDrop: PASS\n";
}

static void TestCatalogCreateSchema() {
    BufferManager bm;
    Catalog cat(bm);
    Transaction tx = MakeTx(1, 1);
    cat.CreateSchema("analytics", tx);
    cat.CreateTable("analytics", "events", MakeCols(), tx);
    cat.CommitEntry("analytics", "events", tx.tx_id, 2);

    Transaction reader = MakeTx(2, 5);
    auto* e = cat.GetTable("analytics", "events", reader);
    assert(e != nullptr);
    std::cout << "TestCatalogCreateSchema: PASS\n";
}

static void TestCatalogColumnLookup() {
    BufferManager bm;
    Catalog cat(bm);
    Transaction tx = MakeTx(1, 1);
    cat.CreateTable("main", "t", MakeCols(), tx);
    cat.CommitEntry("main", "t", tx.tx_id, 2);

    Transaction reader = MakeTx(2, 5);
    auto* e = cat.GetTable("main", "t", reader);
    auto names = e->ColumnNames();
    assert(names[0] == "id");
    assert(names[1] == "name");
    auto types = e->ColumnTypes();
    assert(types[0] == TypeId::INT64);
    assert(types[1] == TypeId::VARCHAR);
    std::cout << "TestCatalogColumnLookup: PASS\n";
}

static void TestCatalogDuplicateCreate() {
    BufferManager bm;
    Catalog cat(bm);
    Transaction t1 = MakeTx(1, 1);
    cat.CreateTable("main", "t", MakeCols(), t1);
    cat.CommitEntry("main", "t", t1.tx_id, 2);

    Transaction t2 = MakeTx(2, 5);
    bool threw = false;
    try { cat.CreateTable("main", "t", MakeCols(), t2); }
    catch (const BindError&) { threw = true; }
    assert(threw);
    std::cout << "TestCatalogDuplicateCreate: PASS\n";
}

void RunCatalogTests() {
    TestCatalogCreateTable();
    TestCatalogTableNotFound();
    TestCatalogSchemaNotFound();
    TestCatalogMVCCCreateVisibility();
    TestCatalogDropTable();
    TestCatalogMVCCDropVisibility();
    TestCatalogRollbackCreate();
    TestCatalogRollbackDrop();
    TestCatalogCreateSchema();
    TestCatalogColumnLookup();
    TestCatalogDuplicateCreate();
    std::cout << "\nAll Catalog tests passed.\n";
}
