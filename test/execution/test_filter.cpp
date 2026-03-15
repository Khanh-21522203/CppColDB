#include "execution/operator/physical_filter.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "execution/pipeline_executor.hpp"
#include "execution/pipeline.hpp"
#include "execution/physical_result_collector.hpp"
#include "main/client_context.hpp"
#include "planner/logical_plan/logical_plan.hpp"
#include "common/types.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <memory>

using namespace cppcoldb;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a single-column INT64 DataChunk from a vector of values.
static DataChunk MakeIntChunk(const std::vector<int64_t>& values) {
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64});
    chunk.columns[0].Reset(TypeId::INT64, values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        chunk.columns[0].int_data[i] = values[i];
        chunk.columns[0].validity.set(i);
    }
    chunk.columns[0].count = values.size();
    chunk.count = values.size();
    return chunk;
}

// Build predicate: col[0] op literal.
static std::unique_ptr<LogicalExpr> MakeColOpLit(const std::string& op, int64_t threshold) {
    auto col = std::make_unique<BoundColumnRef>();
    col->column_idx  = 0;
    col->result_type = TypeId::INT64;
    col->name        = "col";

    auto lit = std::make_unique<LogicalLit>();
    lit->value       = Value::Integer(threshold);
    lit->result_type = TypeId::INT64;

    auto pred = std::make_unique<LogicalBinaryOp>();
    pred->op          = op;
    pred->left        = std::move(col);
    pred->right       = std::move(lit);
    pred->result_type = TypeId::BOOLEAN;

    return pred;
}

// Build predicate: (col > lo) AND (col < hi).
static std::unique_ptr<LogicalExpr> MakeRangePred(int64_t lo, int64_t hi) {
    auto left_pred  = MakeColOpLit(">", lo);
    auto right_pred = MakeColOpLit("<", hi);

    auto pred = std::make_unique<LogicalBinaryOp>();
    pred->op          = "AND";
    pred->left        = std::move(left_pred);
    pred->right       = std::move(right_pred);
    pred->result_type = TypeId::BOOLEAN;

    return pred;
}

// Run a PhysicalFilter directly (no pipeline): Execute on input, return output.
static DataChunk RunFilter(PhysicalFilter& filter, const DataChunk& input) {
    ClientContext ctx;
    OperatorState state;
    DataChunk output;
    filter.Execute(input, output, state, ctx);
    return output;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestFilterBasicPredicate() {
    // Build 100 rows: values 1..100.
    std::vector<int64_t> vals(100);
    for (int i = 0; i < 100; ++i) vals[i] = i + 1;
    DataChunk input = MakeIntChunk(vals);

    PhysicalFilter filter;
    filter.predicate = MakeColOpLit(">", 50);

    DataChunk output = RunFilter(filter, input);

    // Values 51..100 pass → 50 rows.
    assert(output.count == 50);
    // Verify all emitted values are > 50.
    for (size_t i = 0; i < output.count; ++i) {
        assert(output.columns[0].int_data[i] > 50);
    }
    std::cout << "TestFilterBasicPredicate: PASS\n";
}

static void TestFilterAllPass() {
    // Values 1..10, predicate col > -1 → all 10 pass.
    std::vector<int64_t> vals = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    DataChunk input = MakeIntChunk(vals);

    PhysicalFilter filter;
    filter.predicate = MakeColOpLit(">", -1);

    DataChunk output = RunFilter(filter, input);
    assert(output.count == 10);
    std::cout << "TestFilterAllPass: PASS\n";
}

static void TestFilterNonePass() {
    // Values 1..10, predicate col > 200 → 0 pass.
    std::vector<int64_t> vals = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    DataChunk input = MakeIntChunk(vals);

    PhysicalFilter filter;
    filter.predicate = MakeColOpLit(">", 200);

    DataChunk output = RunFilter(filter, input);
    assert(output.count == 0);
    std::cout << "TestFilterNonePass: PASS\n";
}

static void TestFilterNullHandling() {
    // 5 rows: values 1,2,3,4,5. Set row 2 as NULL.
    DataChunk input;
    input.Initialize({TypeId::INT64});
    input.columns[0].Reset(TypeId::INT64, 5);
    for (size_t i = 0; i < 5; ++i) {
        input.columns[0].int_data[i] = static_cast<int64_t>(i + 1);
        input.columns[0].validity.set(i);
    }
    // Set row 2 (value=3) as NULL.
    input.columns[0].validity.reset(2);
    input.columns[0].count = 5;
    input.count = 5;

    // Predicate: col > 0. Non-null values 1,2,4,5 all pass; NULL row excluded.
    PhysicalFilter filter;
    filter.predicate = MakeColOpLit(">", 0);

    DataChunk output = RunFilter(filter, input);
    // 4 non-null rows pass (1, 2, 4, 5); NULL row at index 2 is excluded.
    assert(output.count == 4);
    std::cout << "TestFilterNullHandling: PASS\n";
}

static void TestFilterAndPredicate() {
    // Values 1..30, predicate: col > 10 AND col < 20 → rows 11..19 (9 rows).
    std::vector<int64_t> vals(30);
    for (int i = 0; i < 30; ++i) vals[i] = i + 1;
    DataChunk input = MakeIntChunk(vals);

    PhysicalFilter filter;
    filter.predicate = MakeRangePred(10, 20);

    DataChunk output = RunFilter(filter, input);
    assert(output.count == 9);
    // Verify all output values are in (10, 20).
    for (size_t i = 0; i < output.count; ++i) {
        int64_t v = output.columns[0].int_data[i];
        assert(v > 10 && v < 20);
    }
    std::cout << "TestFilterAndPredicate: PASS\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void RunFilterTests() {
    TestFilterBasicPredicate();
    TestFilterAllPass();
    TestFilterNonePass();
    TestFilterNullHandling();
    TestFilterAndPredicate();
    std::cout << "\nAll Filter tests passed.\n";
}
