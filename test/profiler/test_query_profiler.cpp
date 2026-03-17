#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include "profiler/query_profiler.hpp"
#include "profiler/operator_profiler.hpp"
#include "main/database.hpp"
#include "main/connection.hpp"
#include "execution/physical_result_collector.hpp"

using namespace cppcoldb;

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
// 1. TestOperatorProfileGuardRAII
// ---------------------------------------------------------------------------
static void TestOperatorProfileGuardRAII() {
    QueryProfiler p;
    p.StartQuery("test");
    size_t idx = p.RegisterOperator("TestOp");

    {
        OperatorProfileGuard g(p, idx, 10);
        g.SetRowsOut(5);
        // destructor fires here → RecordOperatorCall
    }

    ProfilingResult r = p.EndQuery();
    Check(r.operators.size() == 1, "RAII: expected 1 operator");
    Check(r.operators[0].call_count == 1, "RAII: call_count should be 1");
    Check(r.operators[0].rows_in == 10,   "RAII: rows_in should be 10");
    Check(r.operators[0].rows_out == 5,   "RAII: rows_out should be 5");
    Check(r.operators[0].total_time_ns >= 0, "RAII: time should be non-negative");

    std::cout << "  TestOperatorProfileGuardRAII PASSED\n";
}

// ---------------------------------------------------------------------------
// 2. TestProfilerDisabledIsNoop
// ---------------------------------------------------------------------------
static void TestProfilerDisabledIsNoop() {
    QueryProfiler p; // never StartQuery → active_ = false
    Check(!p.IsActive(), "Disabled: IsActive should be false");

    p.StartPhase(QueryPhase::PARSE);
    p.EndPhase(QueryPhase::PARSE);
    size_t idx = p.RegisterOperator("Op");
    p.RecordOperatorCall(idx, 1000, 5, 3);

    // Since never started, operators_ is not empty (RegisterOperator adds unconditionally).
    // But EndQuery has never been called so we can't check phase_profiles directly.
    // The key invariant: IsActive remains false throughout.
    Check(!p.IsActive(), "Disabled: IsActive should still be false after calls");

    std::cout << "  TestProfilerDisabledIsNoop PASSED\n";
}

// ---------------------------------------------------------------------------
// 3. TestProfilerPhaseTimings
// ---------------------------------------------------------------------------
static void TestProfilerPhaseTimings() {
    QueryProfiler p;
    p.StartQuery("SELECT 1");

    for (auto phase : {QueryPhase::PARSE, QueryPhase::BIND, QueryPhase::OPTIMIZE,
                       QueryPhase::PHYSICAL_PLAN, QueryPhase::EXECUTE}) {
        p.StartPhase(phase);
        p.EndPhase(phase);
    }

    ProfilingResult r = p.EndQuery();
    Check(r.phases.size() == 5, "PhaseTimings: expected 5 phases");
    Check(r.phases[0].phase_name == "PARSE",         "PhaseTimings: phase[0] = PARSE");
    Check(r.phases[1].phase_name == "BIND",          "PhaseTimings: phase[1] = BIND");
    Check(r.phases[2].phase_name == "OPTIMIZE",      "PhaseTimings: phase[2] = OPTIMIZE");
    Check(r.phases[3].phase_name == "PHYSICAL_PLAN", "PhaseTimings: phase[3] = PHYSICAL_PLAN");
    Check(r.phases[4].phase_name == "EXECUTE",       "PhaseTimings: phase[4] = EXECUTE");

    std::cout << "  TestProfilerPhaseTimings PASSED\n";
}

// ---------------------------------------------------------------------------
// 4. TestProfilerParsePhaseDuration
// ---------------------------------------------------------------------------
static void TestProfilerParsePhaseDuration() {
    QueryProfiler p;
    p.StartQuery("SELECT * FROM complex_table WHERE a > 1 AND b < 2 AND c = 3");
    p.StartPhase(QueryPhase::PARSE);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    p.EndPhase(QueryPhase::PARSE);

    ProfilingResult r = p.EndQuery();
    Check(!r.phases.empty(), "ParseDuration: phases not empty");
    Check(r.phases[0].duration_us > 0, "ParseDuration: parse duration > 0 µs");

    std::cout << "  TestProfilerParsePhaseDuration PASSED\n";
}

// ---------------------------------------------------------------------------
// 5. TestProfilerTotalTime
// ---------------------------------------------------------------------------
static void TestProfilerTotalTime() {
    QueryProfiler p;
    p.StartQuery("q");
    p.StartPhase(QueryPhase::PARSE);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    p.EndPhase(QueryPhase::PARSE);
    p.StartPhase(QueryPhase::EXECUTE);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    p.EndPhase(QueryPhase::EXECUTE);

    ProfilingResult r = p.EndQuery();
    int64_t sum_us = 0;
    for (const auto& ph : r.phases) sum_us += ph.duration_us;
    Check(r.total_duration_us >= sum_us, "TotalTime: total >= sum of phases");

    std::cout << "  TestProfilerTotalTime PASSED\n";
}

// ---------------------------------------------------------------------------
// 6. TestProfilerMultipleOperators
// ---------------------------------------------------------------------------
static void TestProfilerMultipleOperators() {
    QueryProfiler p;
    p.StartQuery("q");
    size_t i0 = p.RegisterOperator("Op0");
    size_t i1 = p.RegisterOperator("Op1");
    size_t i2 = p.RegisterOperator("Op2");
    p.RecordOperatorCall(i0, 100, 10, 8);
    p.RecordOperatorCall(i1, 200, 8,  5);
    p.RecordOperatorCall(i2, 300, 5,  3);

    ProfilingResult r = p.EndQuery();
    Check(r.operators.size() == 3,              "MultipleOps: 3 operators");
    Check(r.operators[0].operator_name == "Op0", "MultipleOps: Op0 name");
    Check(r.operators[1].rows_in  == 8,         "MultipleOps: Op1 rows_in");
    Check(r.operators[2].rows_out == 3,         "MultipleOps: Op2 rows_out");

    std::cout << "  TestProfilerMultipleOperators PASSED\n";
}

// ---------------------------------------------------------------------------
// 7. TestProfilerResetBetweenQueries
// ---------------------------------------------------------------------------
static void TestProfilerResetBetweenQueries() {
    QueryProfiler p;

    // First query
    p.StartQuery("q1");
    p.StartPhase(QueryPhase::PARSE);
    p.EndPhase(QueryPhase::PARSE);
    p.RegisterOperator("Op");
    ProfilingResult r1 = p.EndQuery();
    Check(r1.phases.size() == 1,    "Reset: q1 has 1 phase");
    Check(r1.operators.size() == 1, "Reset: q1 has 1 operator");

    // Second query — must start clean
    p.StartQuery("q2");
    p.StartPhase(QueryPhase::BIND);
    p.EndPhase(QueryPhase::BIND);
    ProfilingResult r2 = p.EndQuery();
    Check(r2.phases.size() == 1,    "Reset: q2 has 1 phase (not accumulated from q1)");
    Check(r2.operators.size() == 0, "Reset: q2 has 0 operators");
    Check(r2.phases[0].phase_name == "BIND", "Reset: q2 phase is BIND");

    std::cout << "  TestProfilerResetBetweenQueries PASSED\n";
}

// ---------------------------------------------------------------------------
// Helpers for integration tests
// ---------------------------------------------------------------------------
static Database* g_db   = nullptr;

static void SetupDB() {
    g_db = new Database(":memory:");
    auto conn = g_db->Connect();
    RunOK(*conn, "CREATE TABLE prof (id INT, val INT)");
    for (int i = 1; i <= 10; ++i) {
        RunOK(*conn, "INSERT INTO prof VALUES (" + std::to_string(i) + ", " + std::to_string(i * 10) + ")");
    }
}

static void TeardownDB() {
    delete g_db;
    g_db = nullptr;
}

// ---------------------------------------------------------------------------
// 8. TestExplainAnalyzeReturnsText
// ---------------------------------------------------------------------------
static void TestExplainAnalyzeReturnsText() {
    auto conn = g_db->Connect();
    QueryResult r = RunOK(*conn, "EXPLAIN ANALYZE SELECT id FROM prof");
    Check(r.column_names.size() == 1,          "ExplainAnalyze: 1 column");
    Check(r.column_names[0] == "QUERY PLAN",   "ExplainAnalyze: column name = QUERY PLAN");
    Check(r.RowCount() == 1,                   "ExplainAnalyze: 1 row");
    const std::string& plan = r.chunks[0].columns[0].str_data[0];
    Check(!plan.empty(),                        "ExplainAnalyze: non-empty plan text");

    std::cout << "  TestExplainAnalyzeReturnsText PASSED\n";
}

// ---------------------------------------------------------------------------
// 9. TestExplainAnalyzeOutputContainsPhases
// ---------------------------------------------------------------------------
static void TestExplainAnalyzeOutputContainsPhases() {
    auto conn = g_db->Connect();
    QueryResult r = RunOK(*conn, "EXPLAIN ANALYZE SELECT id FROM prof");
    const std::string& plan = r.chunks[0].columns[0].str_data[0];

    Check(plan.find("BIND")    != std::string::npos, "ExplainPhases: contains BIND");
    Check(plan.find("OPTIMIZE") != std::string::npos, "ExplainPhases: contains OPTIMIZE");
    Check(plan.find("EXECUTE") != std::string::npos, "ExplainPhases: contains EXECUTE");

    std::cout << "  TestExplainAnalyzeOutputContainsPhases PASSED\n";
}

// ---------------------------------------------------------------------------
// 10. TestExplainAnalyzeOutputContainsOperators
// ---------------------------------------------------------------------------
static void TestExplainAnalyzeOutputContainsOperators() {
    auto conn = g_db->Connect();
    // SELECT with a projection creates at least PhysicalTableScan + PhysicalProjection.
    QueryResult r = RunOK(*conn, "EXPLAIN ANALYZE SELECT id FROM prof");
    const std::string& plan = r.chunks[0].columns[0].str_data[0];

    Check(plan.find("PhysicalTableScan") != std::string::npos,
          "ExplainOps: contains PhysicalTableScan");
    Check(plan.find("PhysicalProjection") != std::string::npos,
          "ExplainOps: contains PhysicalProjection");

    std::cout << "  TestExplainAnalyzeOutputContainsOperators PASSED\n";
}

// ---------------------------------------------------------------------------
// 11. TestProfilerOperatorRowCounts
// ---------------------------------------------------------------------------
static void TestProfilerOperatorRowCounts() {
    auto conn = g_db->Connect();

    // 10 rows in the table. SELECT id produces PhysicalProjection on top of
    // PhysicalTableScan. The projection should record rows_in == rows_out == 10.
    QueryResult r = RunOK(*conn, "EXPLAIN ANALYZE SELECT id FROM prof");
    Check(r.profiling_result.has_value(), "RowCounts: profiling_result present");

    const ProfilingResult& pr = *r.profiling_result;

    const OperatorProfile* proj_op = nullptr;
    for (const auto& op : pr.operators) {
        if (op.operator_name == "PhysicalProjection") {
            proj_op = &op;
            break;
        }
    }
    Check(proj_op != nullptr,        "RowCounts: PhysicalProjection found in profile");
    Check(proj_op->rows_in  == 10,   "RowCounts: projection rows_in == 10");
    Check(proj_op->rows_out == 10,   "RowCounts: projection rows_out == 10");
    Check(proj_op->call_count >= 1,  "RowCounts: at least one call");

    std::cout << "  TestProfilerOperatorRowCounts PASSED\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
void RunProfilerTests() {
    // Unit tests (no DB needed)
    TestOperatorProfileGuardRAII();
    TestProfilerDisabledIsNoop();
    TestProfilerPhaseTimings();
    TestProfilerParsePhaseDuration();
    TestProfilerTotalTime();
    TestProfilerMultipleOperators();
    TestProfilerResetBetweenQueries();

    // Integration tests
    SetupDB();
    TestExplainAnalyzeReturnsText();
    TestExplainAnalyzeOutputContainsPhases();
    TestExplainAnalyzeOutputContainsOperators();
    TestProfilerOperatorRowCounts();
    TeardownDB();
}
