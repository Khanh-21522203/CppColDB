#include "execution/operator/physical_limit.hpp"
#include "execution/pipeline_executor.hpp"
#include "execution/pipeline.hpp"
#include "execution/physical_result_collector.hpp"
#include "execution/physical_operator.hpp"
#include "execution/operator_result_type.hpp"
#include "main/client_context.hpp"
#include "common/types.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <memory>
#include <algorithm>

using namespace cppcoldb;

// ---------------------------------------------------------------------------
// ConstantScan: SOURCE that emits a fixed list of INT64 values.
// Re-defined here so this file is self-contained.
// ---------------------------------------------------------------------------

class ConstantScan : public PhysicalOperator {
public:
    explicit ConstantScan(std::vector<int64_t> values,
                          size_t batch_size = STANDARD_VECTOR_SIZE)
        : values_(std::move(values)), batch_size_(batch_size) {
        role = OperatorRole::SOURCE;
        output_types = {TypeId::INT64};
        output_names = {"val"};
    }

    std::unique_ptr<OperatorState> CreateScanState() const override {
        return std::make_unique<ScanState>();
    }

    void InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) override {}

    OperatorResultType GetData(OperatorState& state, DataChunk& output,
                               ClientContext& /*ctx*/) override {
        auto& s = static_cast<ScanState&>(state);
        if (s.row_offset >= values_.size()) return OperatorResultType::FINISHED;

        size_t to_emit = std::min(batch_size_, values_.size() - s.row_offset);
        output.Initialize({TypeId::INT64});
        for (size_t i = 0; i < to_emit; ++i) {
            DataVector src;
            src.Reset(TypeId::INT64, 1);
            src.int_data[0] = values_[s.row_offset + i];
            src.validity.set(0);
            src.count = 1;
            DataVectorAppend(output.columns[0], src, 0);
        }
        output.count = to_emit;
        s.row_offset += to_emit;

        return (s.row_offset >= values_.size())
               ? OperatorResultType::FINISHED
               : OperatorResultType::HAVE_MORE_OUTPUT;
    }

private:
    std::vector<int64_t> values_;
    size_t               batch_size_;
};

// ---------------------------------------------------------------------------
// Run a pipeline: ConstantScan → PhysicalLimit → ResultCollector.
// ---------------------------------------------------------------------------

static QueryResult RunLimitPipeline(ConstantScan& scan, PhysicalLimit& limit) {
    PhysicalResultCollector collector({"val"}, {TypeId::INT64});

    Pipeline p;
    p.source    = &scan;
    p.operators = {&limit};
    p.sink      = &collector;

    ClientContext ctx;
    PipelineExecutor pe(ctx, p);
    pe.Execute();
    return collector.TakeResult();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestLimitBasic() {
    // 1000 rows, limit=10 → 10 rows returned.
    std::vector<int64_t> vals(1000);
    for (int i = 0; i < 1000; ++i) vals[i] = i;
    ConstantScan scan(vals);

    PhysicalLimit limit;
    limit.limit  = 10;
    limit.offset = 0;

    auto result = RunLimitPipeline(scan, limit);
    assert(result.success);
    assert(result.RowCount() == 10);
    std::cout << "TestLimitBasic: PASS\n";
}

static void TestLimitOffset() {
    // 100 rows (values 0..99), limit=5, offset=10 → values 10,11,12,13,14.
    std::vector<int64_t> vals(100);
    for (int i = 0; i < 100; ++i) vals[i] = i;
    ConstantScan scan(vals);

    PhysicalLimit limit;
    limit.limit  = 5;
    limit.offset = 10;

    auto result = RunLimitPipeline(scan, limit);
    assert(result.success);
    assert(result.RowCount() == 5);

    // Collect all output values and verify they are 10..14.
    std::vector<int64_t> out_vals;
    for (auto& chunk : result.chunks) {
        for (size_t i = 0; i < chunk.count; ++i) {
            out_vals.push_back(chunk.columns[0].int_data[i]);
        }
    }
    assert(out_vals.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        assert(out_vals[i] == static_cast<int64_t>(10 + i));
    }
    std::cout << "TestLimitOffset: PASS\n";
}

static void TestLimitNoLimit() {
    // limit=-1 (no limit). Source has 50 rows → all 50 pass through.
    std::vector<int64_t> vals(50);
    for (int i = 0; i < 50; ++i) vals[i] = i;
    ConstantScan scan(vals);

    PhysicalLimit limit;
    limit.limit  = -1;
    limit.offset = 0;

    auto result = RunLimitPipeline(scan, limit);
    assert(result.success);
    assert(result.RowCount() == 50);
    std::cout << "TestLimitNoLimit: PASS\n";
}

static void TestLimitExceedsSource() {
    // limit=1000 on a 5-row source → 5 rows returned (no crash).
    std::vector<int64_t> vals = {1, 2, 3, 4, 5};
    ConstantScan scan(vals);

    PhysicalLimit limit;
    limit.limit  = 1000;
    limit.offset = 0;

    auto result = RunLimitPipeline(scan, limit);
    assert(result.success);
    assert(result.RowCount() == 5);
    std::cout << "TestLimitExceedsSource: PASS\n";
}

static void TestLimitFinishedSignal() {
    // Call PhysicalLimit::Execute directly with a 20-row chunk, limit=5.
    // Expect FINISHED and output has 5 rows.

    // Build a 20-row INT64 chunk.
    DataChunk input;
    input.Initialize({TypeId::INT64});
    input.columns[0].Reset(TypeId::INT64, 20);
    for (size_t i = 0; i < 20; ++i) {
        input.columns[0].int_data[i] = static_cast<int64_t>(i);
        input.columns[0].validity.set(i);
    }
    input.columns[0].count = 20;
    input.count = 20;

    PhysicalLimit limit;
    limit.limit  = 5;
    limit.offset = 0;

    ClientContext ctx;
    // Use CreateOperatorState so the LimitState is properly constructed.
    auto state_ptr = limit.CreateOperatorState();
    DataChunk output;

    OperatorResultType ret = limit.Execute(input, output, *state_ptr, ctx);

    assert(ret == OperatorResultType::FINISHED);
    assert(output.count == 5);
    std::cout << "TestLimitFinishedSignal: PASS\n";
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void RunLimitTests() {
    TestLimitBasic();
    TestLimitOffset();
    TestLimitNoLimit();
    TestLimitExceedsSource();
    TestLimitFinishedSignal();
    std::cout << "\nAll Limit tests passed.\n";
}
