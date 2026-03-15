#include "execution/operator/physical_projection.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "execution/pipeline.hpp"
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

// Build a 2-column INT64 chunk with given values for col 0 and col 1.
static DataChunk MakeTwoColChunk(const std::vector<int64_t>& a_vals,
                                  const std::vector<int64_t>& b_vals) {
    assert(a_vals.size() == b_vals.size());
    size_t n = a_vals.size();

    DataChunk chunk;
    chunk.Initialize({TypeId::INT64, TypeId::INT64});

    chunk.columns[0].Reset(TypeId::INT64, n);
    chunk.columns[1].Reset(TypeId::INT64, n);
    for (size_t i = 0; i < n; ++i) {
        chunk.columns[0].int_data[i] = a_vals[i];
        chunk.columns[0].validity.set(i);
        chunk.columns[1].int_data[i] = b_vals[i];
        chunk.columns[1].validity.set(i);
    }
    chunk.columns[0].count = n;
    chunk.columns[1].count = n;
    chunk.count = n;
    return chunk;
}

// Build a BoundColumnRef expression for the given chunk column index.
static std::unique_ptr<LogicalExpr> MakeColRef(size_t idx, TypeId type) {
    auto ref = std::make_unique<BoundColumnRef>();
    ref->column_idx  = idx;
    ref->result_type = type;
    ref->name        = "col";
    return ref;
}

// Build an arithmetic binary op expression (left op right).
static std::unique_ptr<LogicalExpr> MakeBinOp(const std::string& op,
                                                std::unique_ptr<LogicalExpr> left,
                                                std::unique_ptr<LogicalExpr> right,
                                                TypeId result_type) {
    auto expr = std::make_unique<LogicalBinaryOp>();
    expr->op          = op;
    expr->left        = std::move(left);
    expr->right       = std::move(right);
    expr->result_type = result_type;
    return expr;
}

// Run a PhysicalProjection directly: Execute on input, return output.
static DataChunk RunProjection(PhysicalProjection& proj, const DataChunk& input) {
    ClientContext ctx;
    OperatorState state;
    DataChunk output;
    proj.Execute(input, output, state, ctx);
    return output;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestProjectionColumnRef() {
    // 2-column input: a=[1,2,3], b=[4,5,6].
    // Project only column b (index 1).
    DataChunk input = MakeTwoColChunk({1, 2, 3}, {4, 5, 6});

    PhysicalProjection proj;
    proj.exprs.push_back(MakeColRef(1, TypeId::INT64));

    DataChunk output = RunProjection(proj, input);

    assert(output.count == 3);
    assert(output.ColumnCount() == 1);
    assert(output.columns[0].int_data[0] == 4);
    assert(output.columns[0].int_data[1] == 5);
    assert(output.columns[0].int_data[2] == 6);
    std::cout << "TestProjectionColumnRef: PASS\n";
}

static void TestProjectionArithmetic() {
    // 2-column input: a=[1,2,3], b=[10,20,30].
    // Project: a + b → [11, 22, 33].
    DataChunk input = MakeTwoColChunk({1, 2, 3}, {10, 20, 30});

    PhysicalProjection proj;
    proj.exprs.push_back(
        MakeBinOp("+", MakeColRef(0, TypeId::INT64),
                       MakeColRef(1, TypeId::INT64),
                       TypeId::INT64));

    DataChunk output = RunProjection(proj, input);

    assert(output.count == 3);
    assert(output.ColumnCount() == 1);
    assert(output.columns[0].int_data[0] == 11);
    assert(output.columns[0].int_data[1] == 22);
    assert(output.columns[0].int_data[2] == 33);
    std::cout << "TestProjectionArithmetic: PASS\n";
}

static void TestProjectionLiteral() {
    // Any 3-row input; project literal 42 → all rows output 42.
    DataChunk input = MakeTwoColChunk({1, 2, 3}, {4, 5, 6});

    auto lit = std::make_unique<LogicalLit>();
    lit->value       = Value::Integer(42);
    lit->result_type = TypeId::INT64;

    PhysicalProjection proj;
    proj.exprs.push_back(std::move(lit));

    DataChunk output = RunProjection(proj, input);

    assert(output.count == 3);
    assert(output.ColumnCount() == 1);
    for (size_t i = 0; i < 3; ++i) {
        assert(output.columns[0].validity[i]);         // not null
        assert(output.columns[0].int_data[i] == 42);
    }
    std::cout << "TestProjectionLiteral: PASS\n";
}

static void TestProjectionNullPropagation() {
    // a=[1,2,3], b=[10,20,30]. Set a[1] to NULL.
    // Project: a + b → row 1 must be NULL.
    DataChunk input = MakeTwoColChunk({1, 2, 3}, {10, 20, 30});
    input.columns[0].validity.reset(1);  // a[1] is NULL

    PhysicalProjection proj;
    proj.exprs.push_back(
        MakeBinOp("+", MakeColRef(0, TypeId::INT64),
                       MakeColRef(1, TypeId::INT64),
                       TypeId::INT64));

    DataChunk output = RunProjection(proj, input);

    assert(output.count == 3);
    assert(output.ColumnCount() == 1);
    // Row 0: 1 + 10 = 11, not null.
    assert(output.columns[0].validity[0]);
    assert(output.columns[0].int_data[0] == 11);
    // Row 1: NULL (a is null → result is null).
    assert(!output.columns[0].validity[1]);
    // Row 2: 3 + 30 = 33, not null.
    assert(output.columns[0].validity[2]);
    assert(output.columns[0].int_data[2] == 33);
    std::cout << "TestProjectionNullPropagation: PASS\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void RunProjectionTests() {
    TestProjectionColumnRef();
    TestProjectionArithmetic();
    TestProjectionLiteral();
    TestProjectionNullPropagation();
    std::cout << "\nAll Projection tests passed.\n";
}
