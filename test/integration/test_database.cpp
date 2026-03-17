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

// ---------------------------------------------------------------------------
// TestInMemoryBasic — create table, insert, select
// ---------------------------------------------------------------------------
static void TestInMemoryBasic() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE t (id INT, name VARCHAR)");
    RunOK(*conn, "INSERT INTO t VALUES (1, 'Alice')");
    RunOK(*conn, "INSERT INTO t VALUES (2, 'Bob')");

    QueryResult r = RunOK(*conn, "SELECT id, name FROM t");
    Check(r.RowCount() == 2, "TestInMemoryBasic: expected 2 rows");
    Check(r.column_names.size() == 2, "TestInMemoryBasic: expected 2 columns");

    std::cout << "  TestInMemoryBasic PASSED\n";
}

// ---------------------------------------------------------------------------
// TestInMemoryMultiStatement — create + inserts + select in sequence
// ---------------------------------------------------------------------------
static void TestInMemoryMultiStatement() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE nums (n INT)");
    for (int i = 1; i <= 5; ++i) {
        RunOK(*conn, "INSERT INTO nums VALUES (" + std::to_string(i) + ")");
    }

    QueryResult r = RunOK(*conn, "SELECT n FROM nums");
    Check(r.RowCount() == 5, "TestInMemoryMultiStatement: expected 5 rows");

    std::cout << "  TestInMemoryMultiStatement PASSED\n";
}

// ---------------------------------------------------------------------------
// TestInMemoryAggregation — GROUP BY + COUNT/SUM
// ---------------------------------------------------------------------------
static void TestInMemoryAggregation() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE sales (dept VARCHAR, amount INT)");
    RunOK(*conn, "INSERT INTO sales VALUES ('eng', 100)");
    RunOK(*conn, "INSERT INTO sales VALUES ('eng', 200)");
    RunOK(*conn, "INSERT INTO sales VALUES ('hr',  50)");

    QueryResult r = RunOK(*conn, "SELECT dept, SUM(amount) FROM sales GROUP BY dept");
    Check(r.RowCount() == 2, "TestInMemoryAggregation: expected 2 groups");

    std::cout << "  TestInMemoryAggregation PASSED\n";
}

// ---------------------------------------------------------------------------
// TestInMemoryJoin — basic inner join
// ---------------------------------------------------------------------------
static void TestInMemoryJoin() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE depts (did INT, dname VARCHAR)");
    RunOK(*conn, "INSERT INTO depts VALUES (1, 'Eng')");
    RunOK(*conn, "INSERT INTO depts VALUES (2, 'HR')");

    RunOK(*conn, "CREATE TABLE emps (eid INT, edept INT, ename VARCHAR)");
    RunOK(*conn, "INSERT INTO emps VALUES (10, 1, 'Alice')");
    RunOK(*conn, "INSERT INTO emps VALUES (11, 1, 'Bob')");
    RunOK(*conn, "INSERT INTO emps VALUES (12, 2, 'Carol')");

    QueryResult r = RunOK(*conn,
        "SELECT eid, ename, dname FROM emps JOIN depts ON edept = did");
    Check(r.RowCount() == 3, "TestInMemoryJoin: expected 3 rows");

    std::cout << "  TestInMemoryJoin PASSED\n";
}

// ---------------------------------------------------------------------------
// TestInMemoryExplicitTransaction — BEGIN/COMMIT pattern
// ---------------------------------------------------------------------------
static void TestInMemoryExplicitTransaction() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE acct (bal INT)");
    RunOK(*conn, "INSERT INTO acct VALUES (1000)");

    // Explicit transaction: insert inside should be visible after commit
    conn->Begin();
    RunOK(*conn, "INSERT INTO acct VALUES (500)");
    conn->Commit();

    QueryResult r = RunOK(*conn, "SELECT bal FROM acct");
    Check(r.RowCount() == 2, "TestInMemoryExplicitTransaction: expected 2 rows after commit");

    std::cout << "  TestInMemoryExplicitTransaction PASSED\n";
}

// ---------------------------------------------------------------------------
// TestInMemoryRollback — rolled-back inserts not visible
// ---------------------------------------------------------------------------
static void TestInMemoryRollback() {
    Database db(":memory:");
    auto conn = db.Connect();

    RunOK(*conn, "CREATE TABLE tmp (v INT)");
    RunOK(*conn, "INSERT INTO tmp VALUES (1)");

    conn->Begin();
    RunOK(*conn, "INSERT INTO tmp VALUES (99)");
    conn->Rollback();

    QueryResult r = RunOK(*conn, "SELECT v FROM tmp");
    Check(r.RowCount() == 1, "TestInMemoryRollback: expected 1 row after rollback");

    std::cout << "  TestInMemoryRollback PASSED\n";
}

// ---------------------------------------------------------------------------
// TestConnectionRollbackOnClose — auto-rollback on Connection destruction
// ---------------------------------------------------------------------------
static void TestConnectionRollbackOnClose() {
    Database db(":memory:");

    // Create table via one connection
    {
        auto setup = db.Connect();
        RunOK(*setup, "CREATE TABLE foo (x INT)");
        RunOK(*setup, "INSERT INTO foo VALUES (42)");
    }

    // Second connection: begin but never commit
    {
        auto bad_conn = db.Connect();
        bad_conn->Begin();
        RunOK(*bad_conn, "INSERT INTO foo VALUES (99)");
        // bad_conn destroyed here → should rollback
    }

    // Third connection: should only see the first row
    auto verify = db.Connect();
    QueryResult r = RunOK(*verify, "SELECT x FROM foo");
    Check(r.RowCount() == 1, "TestConnectionRollbackOnClose: expected 1 row after auto-rollback");

    std::cout << "  TestConnectionRollbackOnClose PASSED\n";
}

// ---------------------------------------------------------------------------
// TestErrorHandling — invalid SQL returns error, connection stays usable
// ---------------------------------------------------------------------------
static void TestErrorHandling() {
    Database db(":memory:");
    auto conn = db.Connect();

    QueryResult bad = conn->Query("SELECT * FROM nonexistent_table");
    Check(!bad.success, "TestErrorHandling: expected error for nonexistent table");
    Check(!bad.error_message.empty(), "TestErrorHandling: error_message should be non-empty");

    // Connection must still be usable after an error
    RunOK(*conn, "CREATE TABLE ok (v INT)");
    RunOK(*conn, "INSERT INTO ok VALUES (7)");
    QueryResult r = RunOK(*conn, "SELECT v FROM ok");
    Check(r.RowCount() == 1, "TestErrorHandling: expected 1 row after error recovery");

    std::cout << "  TestErrorHandling PASSED\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
void RunDatabaseTests() {
    TestInMemoryBasic();
    TestInMemoryMultiStatement();
    TestInMemoryAggregation();
    TestInMemoryJoin();
    TestInMemoryExplicitTransaction();
    TestInMemoryRollback();
    TestConnectionRollbackOnClose();
    TestErrorHandling();
}
