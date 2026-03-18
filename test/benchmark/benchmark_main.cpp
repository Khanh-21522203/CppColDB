#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "main/connection.hpp"
#include "main/database.hpp"

using namespace cppcoldb;

namespace {

struct BenchConfig {
    int rows = 20000;
    int warmup = 3;
    int iterations = 15;
    int insert_batch = 500;
    std::string db_path = ":memory:";
};

struct BenchCase {
    std::string name;
    std::vector<std::string> statements;
};

struct BenchStats {
    double avg_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double qps = 0.0;
    double avg_rows_out = 0.0;
};

[[noreturn]] void Fail(const std::string& msg) {
    std::cerr << "Benchmark error: " << msg << "\n";
    std::exit(1);
}

QueryResult RunSQL(Connection& conn, const std::string& sql) {
    QueryResult r = conn.Query(sql);
    if (!r.success) {
        Fail("SQL failed: " + r.error_message + " | SQL: " + sql);
    }
    return r;
}

std::string BuildInsertSQL(const std::string& table, int from_id, int to_id) {
    std::string sql = "INSERT INTO " + table + " VALUES ";
    for (int i = from_id; i <= to_id; ++i) {
        if (i != from_id) sql += ",";
        int grp = i % 100;
        int val = (i * 17) % 10000;
        sql += "(" + std::to_string(i) + "," + std::to_string(grp) + "," + std::to_string(val) + ")";
    }
    return sql;
}

std::string BuildDimInsertSQL() {
    std::string sql = "INSERT INTO dim VALUES ";
    for (int g = 0; g < 100; ++g) {
        if (g) sql += ",";
        sql += "(" + std::to_string(g) + ", 'g" + std::to_string(g) + "')";
    }
    return sql;
}

void PopulateBaseData(Connection& conn, const BenchConfig& cfg) {
    RunSQL(conn, "CREATE TABLE bench (id INT, grp INT, val INT)");
    RunSQL(conn, "CREATE TABLE dim (grp INT, name VARCHAR)");
    RunSQL(conn, BuildDimInsertSQL());

    int start = 1;
    while (start <= cfg.rows) {
        int end = std::min(cfg.rows, start + cfg.insert_batch - 1);
        RunSQL(conn, BuildInsertSQL("bench", start, end));
        start = end + 1;
    }

    RunSQL(conn, "CREATE TABLE write_t (id INT, grp INT, val INT)");
}

void ExecCase(Connection& conn, const BenchCase& c, size_t& rows_out_last_stmt) {
    rows_out_last_stmt = 0;
    for (size_t i = 0; i < c.statements.size(); ++i) {
        QueryResult r = RunSQL(conn, c.statements[i]);
        if (i + 1 == c.statements.size()) {
            rows_out_last_stmt = r.RowCount();
        }
    }
}

double Percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    if (sorted.size() == 1) return sorted[0];
    double pos = p * static_cast<double>(sorted.size() - 1);
    size_t lo = static_cast<size_t>(pos);
    size_t hi = std::min(sorted.size() - 1, lo + 1);
    double frac = pos - static_cast<double>(lo);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * frac;
}

BenchStats RunBenchmarkCase(Connection& conn, const BenchCase& c, const BenchConfig& cfg) {
    using clock = std::chrono::steady_clock;

    for (int i = 0; i < cfg.warmup; ++i) {
        size_t rows_out = 0;
        ExecCase(conn, c, rows_out);
    }

    std::vector<double> samples_ms;
    samples_ms.reserve(static_cast<size_t>(cfg.iterations));
    size_t rows_total = 0;

    for (int i = 0; i < cfg.iterations; ++i) {
        size_t rows_out = 0;
        auto t0 = clock::now();
        ExecCase(conn, c, rows_out);
        auto t1 = clock::now();
        double ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
        samples_ms.push_back(ms);
        rows_total += rows_out;
    }

    std::vector<double> sorted = samples_ms;
    std::sort(sorted.begin(), sorted.end());

    BenchStats s;
    s.min_ms = sorted.front();
    s.max_ms = sorted.back();
    s.avg_ms = std::accumulate(samples_ms.begin(), samples_ms.end(), 0.0) /
               static_cast<double>(samples_ms.size());
    s.p50_ms = Percentile(sorted, 0.50);
    s.p95_ms = Percentile(sorted, 0.95);
    s.qps = (s.avg_ms > 0.0) ? (1000.0 / s.avg_ms) : 0.0;
    s.avg_rows_out = static_cast<double>(rows_total) / static_cast<double>(cfg.iterations);
    return s;
}

void PrintUsage(const char* argv0) {
    std::cout
        << "Usage: " << argv0
        << " [--rows N] [--warmup N] [--iters N] [--insert-batch N] [--db PATH]\n"
        << "Defaults: --rows 20000 --warmup 3 --iters 15 --insert-batch 500 --db :memory:\n";
}

BenchConfig ParseArgs(int argc, char** argv) {
    BenchConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need_val = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) Fail("Missing value for " + flag);
            return argv[++i];
        };

        if (a == "--help" || a == "-h") {
            PrintUsage(argv[0]);
            std::exit(0);
        }
        if (a == "--rows") {
            cfg.rows = std::stoi(need_val(a));
            continue;
        }
        if (a == "--warmup") {
            cfg.warmup = std::stoi(need_val(a));
            continue;
        }
        if (a == "--iters") {
            cfg.iterations = std::stoi(need_val(a));
            continue;
        }
        if (a == "--insert-batch") {
            cfg.insert_batch = std::stoi(need_val(a));
            continue;
        }
        if (a == "--db") {
            cfg.db_path = need_val(a);
            continue;
        }

        Fail("Unknown argument: " + a);
    }

    if (cfg.rows <= 0 || cfg.warmup < 0 || cfg.iterations <= 0 || cfg.insert_batch <= 0) {
        Fail("Invalid numeric argument values");
    }
    return cfg;
}

} // namespace

int main(int argc, char** argv) {
    BenchConfig cfg = ParseArgs(argc, argv);

    Database db(cfg.db_path);
    auto conn = db.Connect();
    PopulateBaseData(*conn, cfg);

    std::string insert_stmt = BuildInsertSQL("write_t", 1000000, 1000000 + cfg.insert_batch - 1);

    std::vector<BenchCase> cases = {
        {"read.count_scan", {
            "SELECT COUNT(*) FROM bench"
        }},
        {"read.filter_count", {
            "SELECT COUNT(*) FROM bench WHERE val > 5000"
        }},
        {"read.join_orderby_limit", {
            "SELECT bench.id, dim.name FROM bench JOIN dim ON bench.grp = dim.grp "
            "ORDER BY bench.id DESC LIMIT 100"
        }},
        {"read.orderby_limit", {
            "SELECT id, val FROM bench ORDER BY val DESC LIMIT 100"
        }},
        // Late materialization demo: 1% selectivity (grp=i%100, so grp=42 hits ~1K/100K rows).
        // Scans grp column (filter) first; only reads val column for the ~1K matching rows.
        {"read.filter_project", {
            "SELECT val, grp FROM bench WHERE grp = 42"
        }},
        {"write.insert_rollback", {
            "BEGIN",
            insert_stmt,
            "ROLLBACK"
        }},
        {"write.update_rollback", {
            "BEGIN",
            "UPDATE bench SET val = val + 1 WHERE grp < 10",
            "ROLLBACK"
        }},
        {"write.delete_rollback", {
            "BEGIN",
            "DELETE FROM bench WHERE grp < 10",
            "ROLLBACK"
        }}
    };

    std::cout << "CppColDB benchmark\n";
    std::cout << "rows=" << cfg.rows
              << " warmup=" << cfg.warmup
              << " iterations=" << cfg.iterations
              << " insert_batch=" << cfg.insert_batch
              << " db=" << cfg.db_path << "\n\n";

    std::cout << std::left
              << std::setw(30) << "case"
              << std::right
              << std::setw(10) << "avg_ms"
              << std::setw(10) << "p50"
              << std::setw(10) << "p95"
              << std::setw(10) << "min"
              << std::setw(10) << "max"
              << std::setw(10) << "qps"
              << std::setw(12) << "rows_out"
              << "\n";

    std::cout << std::string(102, '-') << "\n";

    for (const auto& c : cases) {
        BenchStats s = RunBenchmarkCase(*conn, c, cfg);
        std::cout << std::left << std::setw(30) << c.name
                  << std::right
                  << std::setw(10) << std::fixed << std::setprecision(3) << s.avg_ms
                  << std::setw(10) << s.p50_ms
                  << std::setw(10) << s.p95_ms
                  << std::setw(10) << s.min_ms
                  << std::setw(10) << s.max_ms
                  << std::setw(10) << s.qps
                  << std::setw(12) << std::setprecision(1) << s.avg_rows_out
                  << "\n";
    }

    return 0;
}
