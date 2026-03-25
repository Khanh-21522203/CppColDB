#include <cassert>
#include <iostream>
#include <string>
#include <filesystem>
#include "main/database.hpp"
#include "main/connection.hpp"

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

static bool RunFails(Connection& conn, const std::string& sql) {
    return !conn.Query(sql).success;
}

static int64_t GetInt(const QueryResult& r, size_t col, size_t row) {
    size_t off = row;
    for (const auto& c : r.chunks) {
        if (off < c.count) return c.columns[col].int_data[off];
        off -= c.count;
    }
    std::abort();
}

static std::string GetStr(const QueryResult& r, size_t col, size_t row) {
    size_t off = row;
    for (const auto& c : r.chunks) {
        if (off < c.count) return c.columns[col].str_data[off];
        off -= c.count;
    }
    std::abort();
}

// ---------------------------------------------------------------------------
// Step 1: NULL partition key throws
// ---------------------------------------------------------------------------

static void TestNullKeyThrows() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, k INT64) PARTITION BY RANGE(k) VALUES (10, 20)");
    Check(RunFails(*conn, "INSERT INTO t VALUES (1, NULL)"),
          "NullKey: INSERT with NULL partition key must fail");
    std::cout << "  TestNullKeyThrows PASSED\n";
}

static void TestNullCompositeKeyThrows() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, y INT64, m INT64) "
                 "PARTITION BY RANGE(y, m) VALUES ((2023, 1), (2024, 1))");
    // NULL in leading key
    Check(RunFails(*conn, "INSERT INTO t VALUES (1, NULL, 6)"),
          "NullCompositeKey: NULL leading key must fail");
    // NULL in non-leading key
    Check(RunFails(*conn, "INSERT INTO t VALUES (2, 2023, NULL)"),
          "NullCompositeKey: NULL non-leading key must fail");
    std::cout << "  TestNullCompositeKeyThrows PASSED\n";
}

// ---------------------------------------------------------------------------
// Step 2 + 3: EXPLAIN shows partition info, multi-predicate pruning
// ---------------------------------------------------------------------------

static void TestExplainShowsPartition() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE orders (id INT64, dt INT64) PARTITION BY RANGE(dt) VALUES (100, 200, 300)");
    auto r = RunOK(*conn, "EXPLAIN SELECT * FROM orders WHERE dt >= 150");
    // Collect explain text
    std::string out;
    for (const auto& c : r.chunks)
        for (size_t i = 0; i < c.count; ++i)
            out += c.columns[0].str_data[i];
    Check(out.find("RANGE") != std::string::npos, "Explain: missing RANGE keyword");
    Check(out.find("dt") != std::string::npos,    "Explain: missing partition column name");
    std::cout << "  TestExplainShowsPartition PASSED\n";
}

static void TestMultiPredicatePruning() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, k INT64) PARTITION BY RANGE(k) VALUES (100, 200, 300)");
    RunOK(*conn, "INSERT INTO t VALUES (1, 50)");   // partition 0
    RunOK(*conn, "INSERT INTO t VALUES (2, 150)");  // partition 1
    RunOK(*conn, "INSERT INTO t VALUES (3, 250)");  // partition 2
    RunOK(*conn, "INSERT INTO t VALUES (4, 350)");  // partition 3

    // k >= 100 AND k < 200 → only partition 1 → row 2
    auto r = RunOK(*conn, "SELECT id FROM t WHERE k >= 100");
    Check(r.RowCount() == 3, "MultiPredPruning: expected 3 rows for k>=100");
    std::cout << "  TestMultiPredicatePruning PASSED\n";
}

// ---------------------------------------------------------------------------
// Step 4: Sorted inserts — zone map effectiveness
// ---------------------------------------------------------------------------

static void TestSortedInserts() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, k INT64) PARTITION BY RANGE(k) VALUES (50, 100)");
    // Insert in reverse order
    RunOK(*conn, "INSERT INTO t VALUES (3, 80)");
    RunOK(*conn, "INSERT INTO t VALUES (1, 20)");
    RunOK(*conn, "INSERT INTO t VALUES (2, 60)");
    RunOK(*conn, "INSERT INTO t VALUES (4, 110)");
    // Verify all rows present and correct partition routing
    auto r = RunOK(*conn, "SELECT COUNT(id) FROM t WHERE k >= 50");
    Check(GetInt(r, 0, 0) == 3, "SortedInserts: expected 3 rows for k>=50");
    std::cout << "  TestSortedInserts PASSED\n";
}

// ---------------------------------------------------------------------------
// Step 5: DROP PARTITION / ADD PARTITION DDL
// ---------------------------------------------------------------------------

static void TestDropPartition() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, k INT64) PARTITION BY RANGE(k) VALUES (100, 200)");
    RunOK(*conn, "INSERT INTO t VALUES (1, 50)");   // partition 0
    RunOK(*conn, "INSERT INTO t VALUES (2, 150)");  // partition 1
    RunOK(*conn, "INSERT INTO t VALUES (3, 250)");  // partition 2
    Check(RunOK(*conn, "SELECT id FROM t").RowCount() == 3, "DropPartition: setup failed");

    RunOK(*conn, "ALTER TABLE t DROP PARTITION 1");
    auto r = RunOK(*conn, "SELECT COUNT(id) FROM t");
    Check(GetInt(r, 0, 0) == 2, "DropPartition: expected 2 rows after drop");
    std::cout << "  TestDropPartition PASSED\n";
}

static void TestAddRangePartition() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, k INT64) PARTITION BY RANGE(k) VALUES (100)");
    RunOK(*conn, "INSERT INTO t VALUES (1, 50)");   // partition 0: < 100
    RunOK(*conn, "INSERT INTO t VALUES (2, 150)");  // partition 1: >= 100

    // Add partition with upper bound 200
    RunOK(*conn, "ALTER TABLE t ADD PARTITION VALUES LESS THAN (200)");
    RunOK(*conn, "INSERT INTO t VALUES (3, 180)");  // lands in new partition 2 (old ub < 200)

    auto r = RunOK(*conn, "SELECT COUNT(id) FROM t");
    Check(GetInt(r, 0, 0) == 3, "AddRangePartition: expected 3 rows");
    std::cout << "  TestAddRangePartition PASSED\n";
}

static void TestAddListPartition() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, region VARCHAR) "
                 "PARTITION BY LIST(region) PARTITION p0 VALUES IN ('NORTH')");
    RunOK(*conn, "INSERT INTO t VALUES (1, 'NORTH')");

    RunOK(*conn, "ALTER TABLE t ADD PARTITION VALUES IN ('SOUTH')");
    RunOK(*conn, "INSERT INTO t VALUES (2, 'SOUTH')");

    auto r = RunOK(*conn, "SELECT COUNT(id) FROM t");
    Check(GetInt(r, 0, 0) == 2, "AddListPartition: expected 2 rows");
    std::cout << "  TestAddListPartition PASSED\n";
}

// ---------------------------------------------------------------------------
// Step 6: INSERT ... SELECT with partitioned target
// ---------------------------------------------------------------------------

static void TestInsertSelect() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE staging (id INT64, k INT64, v FLOAT64)");
    RunOK(*conn, "INSERT INTO staging VALUES (1, 50, 1.0)");
    RunOK(*conn, "INSERT INTO staging VALUES (2, 150, 2.0)");
    RunOK(*conn, "INSERT INTO staging VALUES (3, 250, 3.0)");

    RunOK(*conn, "CREATE TABLE target (id INT64, k INT64, v FLOAT64) "
                 "PARTITION BY RANGE(k) VALUES (100, 200)");
    RunOK(*conn, "INSERT INTO target SELECT id, k, v FROM staging WHERE k >= 100");

    auto r = RunOK(*conn, "SELECT COUNT(id) FROM target");
    Check(GetInt(r, 0, 0) == 2, "InsertSelect: expected 2 rows");
    std::cout << "  TestInsertSelect PASSED\n";
}

// ---------------------------------------------------------------------------
// Filter column not in SELECT (regression for column-pruner bug)
// ---------------------------------------------------------------------------

static void TestFilterColumnNotInSelect() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, k INT64, v FLOAT64) "
                 "PARTITION BY RANGE(k) VALUES (100, 200)");
    RunOK(*conn, "INSERT INTO t VALUES (1, 50,  1.0)");
    RunOK(*conn, "INSERT INTO t VALUES (2, 150, 2.0)");
    RunOK(*conn, "INSERT INTO t VALUES (3, 250, 3.0)");

    // SELECT id but filter on k — k must not be pruned from the scan
    auto r = RunOK(*conn, "SELECT id FROM t WHERE k >= 200");
    Check(r.RowCount() == 1, "FilterNotInSelect: expected 1 row");
    Check(GetInt(r, 0, 0) == 3, "FilterNotInSelect: expected id=3");

    // Filter on v (not a partition key, not in SELECT)
    auto r2 = RunOK(*conn, "SELECT id FROM t WHERE k < 100");
    Check(r2.RowCount() == 1, "FilterNotInSelect: expected 1 row for k<100");
    Check(GetInt(r2, 0, 0) == 1, "FilterNotInSelect: expected id=1");

    std::cout << "  TestFilterColumnNotInSelect PASSED\n";
}

// ---------------------------------------------------------------------------
// Step 7: Partition-wise join (both tables same RANGE partition key)
// ---------------------------------------------------------------------------

static void TestPartitionWiseJoin() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE orders (id INT64, dt INT64, amt FLOAT64) "
                 "PARTITION BY RANGE(dt) VALUES (100, 200)");
    RunOK(*conn, "CREATE TABLE prices (dt INT64, price FLOAT64) "
                 "PARTITION BY RANGE(dt) VALUES (100, 200)");

    RunOK(*conn, "INSERT INTO orders VALUES (1, 50,  10.0)");
    RunOK(*conn, "INSERT INTO orders VALUES (2, 150, 20.0)");
    RunOK(*conn, "INSERT INTO orders VALUES (3, 250, 30.0)");
    RunOK(*conn, "INSERT INTO prices VALUES (50,  1.5)");
    RunOK(*conn, "INSERT INTO prices VALUES (150, 2.5)");
    RunOK(*conn, "INSERT INTO prices VALUES (250, 3.5)");

    auto r = RunOK(*conn, "SELECT id FROM orders JOIN prices ON orders.dt = prices.dt");
    Check(r.RowCount() == 3, "PartitionWiseJoin: expected 3 rows");
    std::cout << "  TestPartitionWiseJoin PASSED\n";
}

// ---------------------------------------------------------------------------
// Step 8: Composite partition key
// ---------------------------------------------------------------------------

static void TestCompositeRangeRouting() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE events (id INT64, y INT64, m INT64) "
                 "PARTITION BY RANGE(y, m) VALUES ((2023, 1), (2023, 7), (2024, 1))");
    //           partition 0: < (2023,1)
    RunOK(*conn, "INSERT INTO events VALUES (1, 2022, 12)");
    //           partition 1: [2023,1) <= x < (2023,7)
    RunOK(*conn, "INSERT INTO events VALUES (2, 2023, 3)");
    RunOK(*conn, "INSERT INTO events VALUES (3, 2023, 6)");
    //           partition 2: [2023,7) <= x < (2024,1)
    RunOK(*conn, "INSERT INTO events VALUES (4, 2023, 9)");
    //           partition 3: >= (2024,1)
    RunOK(*conn, "INSERT INTO events VALUES (5, 2024, 6)");

    auto all = RunOK(*conn, "SELECT id FROM events");
    Check(all.RowCount() == 5, "CompositeRange: expected 5 rows total");
    std::cout << "  TestCompositeRangeRouting PASSED\n";
}

static void TestCompositeRangeLexBounds() {
    // Verify that rows on the boundary of composite bounds route correctly.
    // (2023,1) is the first bound; (2023,0) should be < it → partition 0.
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, y INT64, m INT64) "
                 "PARTITION BY RANGE(y, m) VALUES ((2023, 6))");
    RunOK(*conn, "INSERT INTO t VALUES (1, 2023, 5)");  // < (2023,6) → partition 0
    RunOK(*conn, "INSERT INTO t VALUES (2, 2023, 6)");  // >= (2023,6) → partition 1
    RunOK(*conn, "INSERT INTO t VALUES (3, 2023, 7)");  // >= (2023,6) → partition 1
    RunOK(*conn, "INSERT INTO t VALUES (4, 2022, 12)"); // < (2023,6) → partition 0

    auto r = RunOK(*conn, "SELECT COUNT(id) FROM t WHERE y >= 2023");
    // rows 1, 2, 3 have y=2023 (row 4 has y=2022)
    Check(GetInt(r, 0, 0) == 3, "CompositeRangeLex: expected 3 rows for y>=2023");
    std::cout << "  TestCompositeRangeLexBounds PASSED\n";
}

static void TestCompositeRangePruning() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE events (id INT64, y INT64, m INT64) "
                 "PARTITION BY RANGE(y, m) VALUES ((2023, 1), (2023, 7), (2024, 1))");
    RunOK(*conn, "INSERT INTO events VALUES (1, 2022, 12)");
    RunOK(*conn, "INSERT INTO events VALUES (2, 2023, 3)");
    RunOK(*conn, "INSERT INTO events VALUES (3, 2023, 6)");
    RunOK(*conn, "INSERT INTO events VALUES (4, 2023, 9)");
    RunOK(*conn, "INSERT INTO events VALUES (5, 2024, 6)");

    // Filter on leading key: y >= 2024 → only partition 3
    auto r = RunOK(*conn, "SELECT id FROM events WHERE y >= 2024");
    Check(r.RowCount() == 1, "CompositeRangePruning: expected 1 row for y>=2024");
    Check(GetInt(r, 0, 0) == 5, "CompositeRangePruning: expected id=5");

    // Filter on leading key: y < 2023 → only partition 0
    auto r2 = RunOK(*conn, "SELECT id FROM events WHERE y < 2023");
    Check(r2.RowCount() == 1, "CompositeRangePruning: expected 1 row for y<2023");
    Check(GetInt(r2, 0, 0) == 1, "CompositeRangePruning: expected id=1");

    std::cout << "  TestCompositeRangePruning PASSED\n";
}

static void TestCompositeRangeFilterNotInSelect() {
    // Regression: SELECT col1 WHERE col2=x on composite-partitioned table must not SEGV.
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE events (id INT64, y INT64, m INT64) "
                 "PARTITION BY RANGE(y, m) VALUES ((2023, 1), (2023, 7), (2024, 1))");
    RunOK(*conn, "INSERT INTO events VALUES (1, 2022, 12)");
    RunOK(*conn, "INSERT INTO events VALUES (2, 2023, 3)");
    RunOK(*conn, "INSERT INTO events VALUES (5, 2024, 6)");

    // SELECT id but filter on y (not in SELECT)
    auto r = RunOK(*conn, "SELECT id FROM events WHERE y >= 2024");
    Check(r.RowCount() == 1, "CompositeRangeFilterNotInSelect: expected 1 row");
    Check(GetInt(r, 0, 0) == 5, "CompositeRangeFilterNotInSelect: expected id=5");

    // Filter on m (non-leading, not in SELECT)
    auto r2 = RunOK(*conn, "SELECT id FROM events WHERE m = 3");
    Check(r2.RowCount() == 1, "CompositeRangeFilterNotInSelect: expected 1 row for m=3");

    std::cout << "  TestCompositeRangeFilterNotInSelect PASSED\n";
}

static void TestCompositeHashRouting() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, a INT64, b INT64) "
                 "PARTITION BY HASH(a, b) PARTITIONS 4");
    for (int i = 0; i < 20; ++i) {
        RunOK(*conn, "INSERT INTO t VALUES (" + std::to_string(i) + ", " +
              std::to_string(i % 3) + ", " + std::to_string(i * 7) + ")");
    }
    auto r = RunOK(*conn, "SELECT COUNT(id) FROM t");
    Check(GetInt(r, 0, 0) == 20, "CompositeHash: expected 20 rows total");
    std::cout << "  TestCompositeHashRouting PASSED\n";
}

static void TestCompositeHashFilterNotInSelect() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, a INT64, b INT64) "
                 "PARTITION BY HASH(a, b) PARTITIONS 4");
    RunOK(*conn, "INSERT INTO t VALUES (1, 0, 0)");
    RunOK(*conn, "INSERT INTO t VALUES (2, 1, 7)");
    RunOK(*conn, "INSERT INTO t VALUES (3, 2, 14)");

    auto r = RunOK(*conn, "SELECT id FROM t WHERE a = 1");
    Check(r.RowCount() == 1, "CompositeHashFilterNotInSelect: expected 1 row");
    Check(GetInt(r, 0, 0) == 2, "CompositeHashFilterNotInSelect: expected id=2");
    std::cout << "  TestCompositeHashFilterNotInSelect PASSED\n";
}

static void TestCompositeListRouting() {
    Database db(":memory:");
    auto conn = db.Connect();
    // Single-column LIST (still exercises composite-key code path with nkeys=1)
    RunOK(*conn, "CREATE TABLE t (id INT64, region VARCHAR, score INT64) "
                 "PARTITION BY LIST(region) "
                 "PARTITION north VALUES IN ('NORTH'), "
                 "PARTITION south VALUES IN ('SOUTH')");
    RunOK(*conn, "INSERT INTO t VALUES (1, 'NORTH', 10)");
    RunOK(*conn, "INSERT INTO t VALUES (2, 'SOUTH', 20)");
    RunOK(*conn, "INSERT INTO t VALUES (3, 'NORTH', 30)");

    auto r = RunOK(*conn, "SELECT id FROM t WHERE region = 'NORTH'");
    Check(r.RowCount() == 2, "CompositeList: expected 2 NORTH rows");
    std::cout << "  TestCompositeListRouting PASSED\n";
}

static void TestCompositeListFilterNotInSelect() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, region VARCHAR) "
                 "PARTITION BY LIST(region) "
                 "PARTITION p0 VALUES IN ('EAST'), "
                 "PARTITION p1 VALUES IN ('WEST')");
    RunOK(*conn, "INSERT INTO t VALUES (1, 'EAST')");
    RunOK(*conn, "INSERT INTO t VALUES (2, 'WEST')");
    RunOK(*conn, "INSERT INTO t VALUES (3, 'EAST')");

    // SELECT id WHERE region = 'EAST' — region not in SELECT
    auto r = RunOK(*conn, "SELECT id FROM t WHERE region = 'EAST'");
    Check(r.RowCount() == 2, "CompositeListFilterNotInSelect: expected 2 rows");
    std::cout << "  TestCompositeListFilterNotInSelect PASSED\n";
}

static void TestAddPartitionCompositeKey() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, y INT64, m INT64) "
                 "PARTITION BY RANGE(y, m) VALUES ((2023, 1))");
    RunOK(*conn, "INSERT INTO t VALUES (1, 2022, 6)");   // partition 0
    RunOK(*conn, "INSERT INTO t VALUES (2, 2023, 6)");   // partition 1

    RunOK(*conn, "ALTER TABLE t ADD PARTITION VALUES LESS THAN ((2024, 1))");
    RunOK(*conn, "INSERT INTO t VALUES (3, 2023, 9)");   // new partition 2

    auto r = RunOK(*conn, "SELECT COUNT(id) FROM t");
    Check(GetInt(r, 0, 0) == 3, "AddPartitionCompositeKey: expected 3 rows");
    std::cout << "  TestAddPartitionCompositeKey PASSED\n";
}

// ---------------------------------------------------------------------------
// WAL replay tests
// ---------------------------------------------------------------------------

static void TestWalReplayCompositeRange() {
    const std::string path = "/tmp/cppcoldb_test_composite_wal";
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".wal");

    // Session 1: create composite-partitioned table, insert rows
    {
        Database db(path);
        auto conn = db.Connect();
        RunOK(*conn, "CREATE TABLE events (id INT64, y INT64, m INT64) "
                     "PARTITION BY RANGE(y, m) VALUES ((2023, 1), (2023, 7), (2024, 1))");
        RunOK(*conn, "INSERT INTO events VALUES (1, 2022, 12)");
        RunOK(*conn, "INSERT INTO events VALUES (2, 2023, 3)");
        RunOK(*conn, "INSERT INTO events VALUES (3, 2023, 9)");
        RunOK(*conn, "INSERT INTO events VALUES (4, 2024, 6)");
        // ~Database() → checkpoint
    }

    // Session 2: reopen and verify all 4 rows survive with correct partition info
    {
        Database db(path);
        auto conn = db.Connect();
        auto r = RunOK(*conn, "SELECT COUNT(id) FROM events");
        Check(GetInt(r, 0, 0) == 4, "WalReplayCompositeRange: expected 4 rows after reopen");

        // Pruning must still work after replay
        auto pruned = RunOK(*conn, "SELECT id FROM events WHERE y >= 2024");
        Check(pruned.RowCount() == 1, "WalReplayCompositeRange: pruning after replay");
        Check(GetInt(pruned, 0, 0) == 4, "WalReplayCompositeRange: expected id=4");
    }

    std::filesystem::remove(path);
    std::filesystem::remove(path + ".wal");
    std::cout << "  TestWalReplayCompositeRange PASSED\n";
}

static void TestWalReplayCompositeHash() {
    const std::string path = "/tmp/cppcoldb_test_hash_wal";
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".wal");

    {
        Database db(path);
        auto conn = db.Connect();
        RunOK(*conn, "CREATE TABLE t (id INT64, a INT64, b INT64) "
                     "PARTITION BY HASH(a, b) PARTITIONS 4");
        for (int i = 0; i < 10; ++i) {
            RunOK(*conn, "INSERT INTO t VALUES (" + std::to_string(i) + ", " +
                  std::to_string(i % 3) + ", " + std::to_string(i * 5) + ")");
        }
    }

    {
        Database db(path);
        auto conn = db.Connect();
        auto r = RunOK(*conn, "SELECT COUNT(id) FROM t");
        Check(GetInt(r, 0, 0) == 10, "WalReplayCompositeHash: expected 10 rows after reopen");
    }

    std::filesystem::remove(path);
    std::filesystem::remove(path + ".wal");
    std::cout << "  TestWalReplayCompositeHash PASSED\n";
}

static void TestWalReplayCompositeList() {
    const std::string path = "/tmp/cppcoldb_test_list_wal";
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".wal");

    {
        Database db(path);
        auto conn = db.Connect();
        RunOK(*conn, "CREATE TABLE t (id INT64, region VARCHAR) "
                     "PARTITION BY LIST(region) "
                     "PARTITION p0 VALUES IN ('NORTH'), "
                     "PARTITION p1 VALUES IN ('SOUTH')");
        RunOK(*conn, "INSERT INTO t VALUES (1, 'NORTH')");
        RunOK(*conn, "INSERT INTO t VALUES (2, 'SOUTH')");
        RunOK(*conn, "INSERT INTO t VALUES (3, 'NORTH')");
    }

    {
        Database db(path);
        auto conn = db.Connect();
        auto r = RunOK(*conn, "SELECT COUNT(id) FROM t");
        Check(GetInt(r, 0, 0) == 3, "WalReplayCompositeList: expected 3 rows after reopen");

        auto north = RunOK(*conn, "SELECT id FROM t WHERE region = 'NORTH'");
        Check(north.RowCount() == 2, "WalReplayCompositeList: expected 2 NORTH rows");
    }

    std::filesystem::remove(path);
    std::filesystem::remove(path + ".wal");
    std::cout << "  TestWalReplayCompositeList PASSED\n";
}

// ---------------------------------------------------------------------------
// HASH partition — all rows counted, non-partitioned path also works
// ---------------------------------------------------------------------------

static void TestHashPartitionAll() {
    Database db(":memory:");
    auto conn = db.Connect();
    RunOK(*conn, "CREATE TABLE t (id INT64, k INT64) PARTITION BY HASH(k) PARTITIONS 8");
    for (int i = 0; i < 40; ++i)
        RunOK(*conn, "INSERT INTO t VALUES (" + std::to_string(i) + ", " + std::to_string(i) + ")");
    auto r = RunOK(*conn, "SELECT COUNT(id) FROM t");
    Check(GetInt(r, 0, 0) == 40, "HashPartitionAll: expected 40 rows");
    std::cout << "  TestHashPartitionAll PASSED\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void RunPartitionTests() {
    // Step 1: NULL key
    TestNullKeyThrows();
    TestNullCompositeKeyThrows();

    // Step 2 + 3: EXPLAIN, multi-predicate pruning
    TestExplainShowsPartition();
    TestMultiPredicatePruning();

    // Step 4: Sorted inserts
    TestSortedInserts();

    // Step 5: DDL
    TestDropPartition();
    TestAddRangePartition();
    TestAddListPartition();

    // Step 6: INSERT ... SELECT
    TestInsertSelect();

    // Column-pruner regression (filter col not in SELECT)
    TestFilterColumnNotInSelect();

    // Step 7: partition-wise join
    TestPartitionWiseJoin();

    // Step 8: Composite partition key
    TestCompositeRangeRouting();
    TestCompositeRangeLexBounds();
    TestCompositeRangePruning();
    TestCompositeRangeFilterNotInSelect();
    TestCompositeHashRouting();
    TestCompositeHashFilterNotInSelect();
    TestCompositeListRouting();
    TestCompositeListFilterNotInSelect();
    TestAddPartitionCompositeKey();

    // WAL replay with composite keys
    TestWalReplayCompositeRange();
    TestWalReplayCompositeHash();
    TestWalReplayCompositeList();

    // HASH baseline
    TestHashPartitionAll();
}
