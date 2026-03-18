#include <cassert>
#include <iostream>
#include <string>
#include <filesystem>
#include "main/database.hpp"
#include "main/connection.hpp"
#include "execution/physical_result_collector.hpp"

using namespace cppcoldb;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void Check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::abort();
    }
}

static QueryResult RunOK(Connection& conn, const std::string& sql) {
    QueryResult r = conn.Query(sql);
    if (!r.success) {
        std::cerr << "FAIL (sql error): " << r.error_message << "\nSQL: " << sql << "\n";
        std::abort();
    }
    return r;
}

static int64_t GetIntAt(const QueryResult& r, size_t col, size_t row) {
    size_t offset = row;
    for (const auto& chunk : r.chunks) {
        if (offset < chunk.count) return chunk.columns[col].int_data[offset];
        offset -= chunk.count;
    }
    std::abort();
}

static std::string GetStrAt(const QueryResult& r, size_t col, size_t row) {
    size_t offset = row;
    for (const auto& chunk : r.chunks) {
        if (offset < chunk.count) return chunk.columns[col].str_data[offset];
        offset -= chunk.count;
    }
    std::abort();
}

// ---------------------------------------------------------------------------
// 1. TestE2EInMemorySelectValues
// ---------------------------------------------------------------------------
static void TestE2EInMemorySelectValues() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE t (id INT, val INT)");
    RunOK(*conn, "INSERT INTO t VALUES (10, 100)");
    RunOK(*conn, "INSERT INTO t VALUES (20, 200)");
    RunOK(*conn, "INSERT INTO t VALUES (30, 300)");

    QueryResult r = RunOK(*conn, "SELECT id, val FROM t");
    Check(r.RowCount() == 3, "E2EValues: expected 3 rows");

    int64_t sum_id = 0, sum_val = 0;
    for (size_t i = 0; i < r.RowCount(); ++i) {
        sum_id  += GetIntAt(r, 0, i);
        sum_val += GetIntAt(r, 1, i);
    }
    Check(sum_id  == 60,  "E2EValues: sum of ids should be 60");
    Check(sum_val == 600, "E2EValues: sum of vals should be 600");

    std::cout << "  TestE2EInMemorySelectValues PASSED\n";
}

// ---------------------------------------------------------------------------
// 2. TestE2EPersistentDB
// ---------------------------------------------------------------------------
static void TestE2EPersistentDB() {
    const std::string db_path = "/tmp/cppcoldb_e2e_persist";
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + ".wal");

    // Session 1: create, insert, then shutdown → checkpoint
    {
        Database db(db_path);
        auto conn = db.Connect();
        RunOK(*conn, "CREATE TABLE persist_t (id INT, val INT)");
        RunOK(*conn, "INSERT INTO persist_t VALUES (1, 100)");
        RunOK(*conn, "INSERT INTO persist_t VALUES (2, 200)");
        RunOK(*conn, "INSERT INTO persist_t VALUES (3, 300)");
        // ~Database() → Shutdown() → CreateCheckpoint()
    }

    // Session 2: reopen and verify data persists
    {
        Database db(db_path);
        auto conn = db.Connect();
        QueryResult r = RunOK(*conn, "SELECT id, val FROM persist_t");
        Check(r.RowCount() == 3, "Persistent: expected 3 rows after reopen");

        int64_t sum_id = 0, sum_val = 0;
        for (size_t i = 0; i < r.RowCount(); ++i) {
            sum_id  += GetIntAt(r, 0, i);
            sum_val += GetIntAt(r, 1, i);
        }
        Check(sum_id  == 6,   "Persistent: sum of ids should be 6");
        Check(sum_val == 600, "Persistent: sum of vals should be 600");
    }

    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + ".wal");

    std::cout << "  TestE2EPersistentDB PASSED\n";
}

// ---------------------------------------------------------------------------
// 3. TestE2EMVCCIsolation
// ---------------------------------------------------------------------------
static void TestE2EMVCCIsolation() {
    Database db(":memory:");

    // Setup table with one committed row
    {
        auto setup = db.Connect();
        RunOK(*setup, "CREATE TABLE mvcc_t (v INT)");
        RunOK(*setup, "INSERT INTO mvcc_t VALUES (1)");
    }

    // conn1: begin explicit transaction, insert row 2 (uncommitted)
    auto conn1 = db.Connect();
    conn1->Begin();
    RunOK(*conn1, "INSERT INTO mvcc_t VALUES (2)");

    // conn2: should only see row 1 (conn1's row not yet committed)
    auto conn2 = db.Connect();
    QueryResult r1 = RunOK(*conn2, "SELECT v FROM mvcc_t");
    Check(r1.RowCount() == 1, "MVCC: conn2 should not see conn1 uncommitted row");

    // conn1 commits
    conn1->Commit();

    // conn2 queries again with a new auto-commit transaction → should see row 2
    QueryResult r2 = RunOK(*conn2, "SELECT v FROM mvcc_t");
    Check(r2.RowCount() == 2, "MVCC: conn2 should see conn1 committed row");

    std::cout << "  TestE2EMVCCIsolation PASSED\n";
}

// ---------------------------------------------------------------------------
// 4. TestE2EGroupByValues
// ---------------------------------------------------------------------------
static void TestE2EGroupByValues() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE sales (dept VARCHAR, amount INT)");
    RunOK(*conn, "INSERT INTO sales VALUES ('eng', 100)");
    RunOK(*conn, "INSERT INTO sales VALUES ('eng', 200)");
    RunOK(*conn, "INSERT INTO sales VALUES ('hr',  50)");

    QueryResult r = RunOK(*conn, "SELECT dept, SUM(amount) FROM sales GROUP BY dept");
    Check(r.RowCount() == 2, "GroupBy: expected 2 groups");

    // Hash table order is non-deterministic; scan to find groups
    bool found_eng = false, found_hr = false;
    for (size_t i = 0; i < r.RowCount(); ++i) {
        std::string dept = GetStrAt(r, 0, i);
        int64_t sum = GetIntAt(r, 1, i);
        if (dept == "eng") {
            Check(sum == 300, "GroupBy: eng sum should be 300");
            found_eng = true;
        }
        if (dept == "hr") {
            Check(sum == 50, "GroupBy: hr sum should be 50");
            found_hr = true;
        }
    }
    Check(found_eng, "GroupBy: should find eng group");
    Check(found_hr,  "GroupBy: should find hr group");

    std::cout << "  TestE2EGroupByValues PASSED\n";
}

// ---------------------------------------------------------------------------
// 5. TestE2EJoinValues
// ---------------------------------------------------------------------------
static void TestE2EJoinValues() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE depts (did INT, dname VARCHAR)");
    RunOK(*conn, "INSERT INTO depts VALUES (1, 'Eng')");
    RunOK(*conn, "INSERT INTO depts VALUES (2, 'HR')");

    RunOK(*conn, "CREATE TABLE emps (eid INT, dept_id INT)");
    RunOK(*conn, "INSERT INTO emps VALUES (10, 1)");
    RunOK(*conn, "INSERT INTO emps VALUES (11, 2)");
    RunOK(*conn, "INSERT INTO emps VALUES (12, 1)");

    QueryResult r = RunOK(*conn,
        "SELECT eid, dname FROM emps JOIN depts ON dept_id = did");
    Check(r.RowCount() == 3, "Join: expected 3 rows");

    int64_t eng_count = 0, hr_count = 0;
    for (size_t i = 0; i < r.RowCount(); ++i) {
        std::string dname = GetStrAt(r, 1, i);
        if (dname == "Eng") ++eng_count;
        if (dname == "HR")  ++hr_count;
    }
    Check(eng_count == 2, "Join: expected 2 Eng rows");
    Check(hr_count  == 1, "Join: expected 1 HR row");

    std::cout << "  TestE2EJoinValues PASSED\n";
}

// ---------------------------------------------------------------------------
// 6. TestE2EUpdateRollbackAndPersistedUpdate
// ---------------------------------------------------------------------------
static void TestE2EUpdateRollbackAndPersistedUpdate() {
    // Rollback should restore original values.
    {
        Database db(":memory:");
        auto conn = db.Connect();
        RunOK(*conn, "CREATE TABLE t (a INT)");
        RunOK(*conn, "INSERT INTO t VALUES (1)");
        QueryResult bad_begin = conn->Query("BEGIN nonsense");
        Check(!bad_begin.success, "TxnKeyword: BEGIN with trailing garbage must fail");
        RunOK(*conn, "BEGIN");
        RunOK(*conn, "UPDATE t SET a = 2");
        RunOK(*conn, "ROLLBACK");
        QueryResult r = RunOK(*conn, "SELECT a FROM t");
        Check(r.RowCount() == 1, "UpdateRollback: expected one row");
        Check(GetIntAt(r, 0, 0) == 1, "UpdateRollback: rollback must restore old value");
    }

    // Updating a persisted (flushed) row should succeed.
    const std::string db_path = "/tmp/cppcoldb_e2e_update_persist";
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + ".wal");
    {
        Database db(db_path);
        auto conn = db.Connect();
        RunOK(*conn, "CREATE TABLE t (a INT)");
        RunOK(*conn, "INSERT INTO t VALUES (1)");
    }
    {
        Database db(db_path);
        auto conn = db.Connect();
        RunOK(*conn, "UPDATE t SET a = 2");
        QueryResult r = RunOK(*conn, "SELECT a FROM t");
        Check(r.RowCount() == 1, "PersistedUpdate: expected one row");
        Check(GetIntAt(r, 0, 0) == 2, "PersistedUpdate: updated value should persist");
    }
    std::filesystem::remove(db_path);
    std::filesystem::remove(db_path + ".wal");

    std::cout << "  TestE2EUpdateRollbackAndPersistedUpdate PASSED\n";
}

// ---------------------------------------------------------------------------
// 7. TestE2EAggregateJoinOrderBy
// ---------------------------------------------------------------------------
static void TestE2EAggregateJoinOrderBy() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE a (id INT, k INT)");
    RunOK(*conn, "CREATE TABLE b (k INT)");
    RunOK(*conn, "INSERT INTO a VALUES (1, 1)");
    RunOK(*conn, "INSERT INTO a VALUES (2, 2)");
    RunOK(*conn, "INSERT INTO a VALUES (3, 2)");
    RunOK(*conn, "INSERT INTO b VALUES (1)");
    RunOK(*conn, "INSERT INTO b VALUES (2)");

    QueryResult r = RunOK(*conn,
        "SELECT a.k, COUNT(*) FROM a JOIN b ON a.k = b.k "
        "GROUP BY a.k ORDER BY a.k DESC");

    Check(r.RowCount() == 2, "AggJoinOrderBy: expected two groups");
    Check(GetIntAt(r, 0, 0) == 2, "AggJoinOrderBy: first row should be k=2 (DESC)");
    Check(GetIntAt(r, 0, 1) == 1, "AggJoinOrderBy: second row should be k=1 (DESC)");

    std::cout << "  TestE2EAggregateJoinOrderBy PASSED\n";
}

// ---------------------------------------------------------------------------
// 8. TestE2EOrderByLimitOffset
// ---------------------------------------------------------------------------
static void TestE2EOrderByLimitOffset() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE t (id INT, score INT)");
    for (int i = 0; i < 10; ++i) {
        RunOK(*conn, "INSERT INTO t VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ")");
    }

    QueryResult r = RunOK(*conn,
        "SELECT id, score FROM t ORDER BY score DESC LIMIT 3 OFFSET 2");

    Check(r.RowCount() == 3, "OrderByLimitOffset: expected 3 rows");
    Check(GetIntAt(r, 1, 0) == 7, "OrderByLimitOffset: first score should be 7");
    Check(GetIntAt(r, 1, 1) == 6, "OrderByLimitOffset: second score should be 6");
    Check(GetIntAt(r, 1, 2) == 5, "OrderByLimitOffset: third score should be 5");

    std::cout << "  TestE2EOrderByLimitOffset PASSED\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
void RunEndToEndTests() {
    TestE2EInMemorySelectValues();
    TestE2EPersistentDB();
    TestE2EMVCCIsolation();
    TestE2EGroupByValues();
    TestE2EJoinValues();
    TestE2EUpdateRollbackAndPersistedUpdate();
    TestE2EAggregateJoinOrderBy();
    TestE2EOrderByLimitOffset();
}
