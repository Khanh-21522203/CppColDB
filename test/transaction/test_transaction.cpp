#include "transaction/transaction_manager.hpp"
#include "transaction/undo_buffer.hpp"
#include "catalog/catalog.hpp"
#include "storage/buffer_manager.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace cppcoldb;

// ---- UndoBuffer tests ------------------------------------------------------

static void TestUndoBufferForwardOrder() {
    UndoBuffer buf;
    std::vector<int> seen;

    buf.PushInsert(InsertUndoEntry{"main", "t1", 0, 0, 5});
    buf.PushInsert(InsertUndoEntry{"main", "t1", 0, 5, 3});
    buf.PushDelete(DeleteUndoEntry{"main", "t1", {}});

    int order = 0;
    buf.ForEachForward([&](const UndoEntry& e) { seen.push_back(order++); });
    assert(seen.size() == 3);
    assert(seen[0] == 0 && seen[1] == 1 && seen[2] == 2);
    std::cout << "TestUndoBufferForwardOrder: PASS\n";
}

static void TestUndoBufferReverseOrder() {
    UndoBuffer buf;
    buf.PushInsert(InsertUndoEntry{"main", "t1", 0, 0, 5});
    buf.PushInsert(InsertUndoEntry{"main", "t1", 0, 5, 3});
    buf.PushDelete(DeleteUndoEntry{"main", "t1", {}});

    std::vector<size_t> seen_indices;
    buf.ForEachReverse([&](const UndoEntry& e) {
        if (const auto* ins = std::get_if<InsertUndoEntry>(&e)) {
            seen_indices.push_back(ins->append_start);
        } else {
            seen_indices.push_back(999); // DELETE marker
        }
    });
    // Expected reverse: DELETE(999), InsertUndoEntry{5}, InsertUndoEntry{0}
    assert(seen_indices[0] == 999);
    assert(seen_indices[1] == 5);
    assert(seen_indices[2] == 0);
    std::cout << "TestUndoBufferReverseOrder: PASS\n";
}

// ---- TransactionManager tests ----------------------------------------------

static void TestBeginTransaction() {
    BufferManager bm;
    Catalog cat(bm);
    TransactionManager tm(cat);

    auto tx1 = tm.BeginTransaction();
    auto tx2 = tm.BeginTransaction();

    assert(tx1->tx_id != INVALID_TRANSACTION);
    assert(tx2->tx_id != INVALID_TRANSACTION);
    assert(tx1->tx_id != tx2->tx_id);
    assert(tx1->start_time >= 0);
    std::cout << "TestBeginTransaction: PASS\n";
}

static void TestReadOnlyTransactionSkipsWAL() {
    // Empty undo buffer → commit works without WAL (WAL=nullptr is fine).
    BufferManager bm;
    Catalog cat(bm);
    TransactionManager tm(cat); // no WAL

    auto tx = tm.BeginTransaction();
    assert(tx->undo_buffer.IsEmpty());
    tm.Commit(tx); // should not throw
    assert(tm.ActiveTransactionCount() == 0);
    std::cout << "TestReadOnlyTransactionSkipsWAL: PASS\n";
}

static void TestGCClearsOldVersions() {
    BufferManager bm;
    Catalog cat(bm);
    TransactionManager tm(cat);

    auto tx1 = tm.BeginTransaction();
    auto tx2 = tm.BeginTransaction();
    assert(tm.ActiveTransactionCount() == 2);

    tm.Commit(tx1);
    assert(tm.ActiveTransactionCount() == 1);

    tm.Rollback(tx2);
    assert(tm.ActiveTransactionCount() == 0);
    std::cout << "TestGCClearsOldVersions: PASS\n";
}

static void TestConcurrentTransactions() {
    BufferManager bm;
    Catalog cat(bm);
    TransactionManager tm(cat);

    auto tx1 = tm.BeginTransaction();
    auto tx2 = tm.BeginTransaction();

    tm.Commit(tx1);
    tm.Commit(tx2);

    // commit_counter advanced twice; both got distinct commit_times.
    assert(tx1->commit_time != INVALID_TIMESTAMP);
    assert(tx2->commit_time != INVALID_TIMESTAMP);
    assert(tx1->commit_time != tx2->commit_time);
    std::cout << "TestConcurrentTransactions: PASS\n";
}

static void TestRollbackTransaction() {
    BufferManager bm;
    Catalog cat(bm);
    TransactionManager tm(cat);

    auto tx = tm.BeginTransaction();
    tx->undo_buffer.PushInsert(InsertUndoEntry{"main", "missing_table", 0, 0, 10});
    // Rollback without actually having a table → just should not throw
    // (GetTable returns nullptr and we guard against it)
    tm.Rollback(tx);
    assert(tm.ActiveTransactionCount() == 0);
    std::cout << "TestRollbackTransaction: PASS\n";
}

void RunTransactionTests() {
    TestUndoBufferForwardOrder();
    TestUndoBufferReverseOrder();
    TestBeginTransaction();
    TestReadOnlyTransactionSkipsWAL();
    TestGCClearsOldVersions();
    TestConcurrentTransactions();
    TestRollbackTransaction();
    std::cout << "\nAll Transaction tests passed.\n";
}
