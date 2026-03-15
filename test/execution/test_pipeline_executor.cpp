#include "execution/executor.hpp"
#include "execution/pipeline_executor.hpp"
#include "main/client_context.hpp"
#include "common/types.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace cppcoldb;

// ---- Toy operators ---------------------------------------------------------

// SOURCE: emits batches of INT64 values.
class PhysicalConstantScan : public PhysicalOperator {
public:
    explicit PhysicalConstantScan(std::vector<int64_t> values,
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
            DataVector src; src.Reset(TypeId::INT64, 1);
            src.int_data[0] = values_[s.row_offset + i];
            src.validity.set(0); src.count = 1;
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

// OPERATOR: passes input to output unchanged.
class PhysicalIdentityOperator : public PhysicalOperator {
public:
    explicit PhysicalIdentityOperator(TypeId type) {
        role = OperatorRole::OPERATOR;
        output_types = {type};
        output_names = {"val"};
    }

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                                OperatorState& /*state*/,
                                ClientContext& /*ctx*/) override {
        output.Initialize(output_types);
        std::vector<uint32_t> indices(input.count);
        for (uint32_t i = 0; i < static_cast<uint32_t>(input.count); ++i)
            indices[i] = i;
        DataChunkSlice(output, input, indices);
        // NEED_MORE_INPUT: processed all rows from this chunk; fetch next input.
        return OperatorResultType::NEED_MORE_INPUT;
    }
};

// OPERATOR state for buffering operator.
struct BufferState : OperatorState {
    DataChunk buffer;
    bool      emitted = false;
    int       chunks_received = 0;
};

// OPERATOR: buffers 2 input chunks then emits combined output once.
class PhysicalBufferTwo : public PhysicalOperator {
public:
    PhysicalBufferTwo() {
        role = OperatorRole::OPERATOR;
        output_types = {TypeId::INT64};
        output_names = {"val"};
    }

    std::unique_ptr<OperatorState> CreateOperatorState() const override {
        return std::make_unique<BufferState>();
    }

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                                OperatorState& raw_state,
                                ClientContext& /*ctx*/) override {
        auto& s = static_cast<BufferState&>(raw_state);
        if (s.emitted) return OperatorResultType::NEED_MORE_INPUT;

        // Initialize buffer if needed.
        if (s.buffer.columns.empty()) s.buffer.Initialize({TypeId::INT64});

        for (size_t i = 0; i < input.count; ++i) {
            DataVectorAppend(s.buffer.columns[0], input.columns[0], i);
        }
        s.buffer.count += input.count;
        s.chunks_received++;

        if (s.chunks_received < 2) return OperatorResultType::NEED_MORE_INPUT;

        // Emit combined buffer.
        output.Initialize({TypeId::INT64});
        std::vector<uint32_t> idx(s.buffer.count);
        for (uint32_t i = 0; i < static_cast<uint32_t>(s.buffer.count); ++i) idx[i] = i;
        DataChunkSlice(output, s.buffer, idx);
        s.emitted = true;
        // NEED_MORE_INPUT: emitted all buffered rows; done with this input chunk.
        return OperatorResultType::NEED_MORE_INPUT;
    }
};

// OPERATOR: for each input row, returns two output chunks (one copy each).
struct DoubleState : OperatorState {
    bool second_pass = false;
    DataChunk saved_input;
};

class PhysicalDoubleOutput : public PhysicalOperator {
public:
    PhysicalDoubleOutput() {
        role = OperatorRole::OPERATOR;
        output_types = {TypeId::INT64};
        output_names = {"val"};
    }

    std::unique_ptr<OperatorState> CreateOperatorState() const override {
        return std::make_unique<DoubleState>();
    }

    OperatorResultType Execute(const DataChunk& input, DataChunk& output,
                                OperatorState& raw_state,
                                ClientContext& /*ctx*/) override {
        auto& s = static_cast<DoubleState&>(raw_state);

        const DataChunk* src = &input;
        if (s.second_pass) src = &s.saved_input;
        else {
            // Save a copy for second pass.
            s.saved_input.Initialize({TypeId::INT64});
            std::vector<uint32_t> idx(input.count);
            for (uint32_t i = 0; i < static_cast<uint32_t>(input.count); ++i) idx[i] = i;
            DataChunkSlice(s.saved_input, input, idx);
        }

        output.Initialize({TypeId::INT64});
        std::vector<uint32_t> idx(src->count);
        for (uint32_t i = 0; i < static_cast<uint32_t>(src->count); ++i) idx[i] = i;
        DataChunkSlice(output, *src, idx);

        if (!s.second_pass) {
            s.second_pass = true;
            // HAVE_MORE_OUTPUT: executor must call again with same input for second copy.
            return OperatorResultType::HAVE_MORE_OUTPUT;
        } else {
            s.second_pass = false;
            // NEED_MORE_INPUT: both copies emitted; done with this input chunk.
            return OperatorResultType::NEED_MORE_INPUT;
        }
    }
};

// ---- Tests -----------------------------------------------------------------

// Build and run a simple pipeline manually (bypass Executor for clarity).
static QueryResult RunManualPipeline(
        PhysicalOperator* source,
        std::vector<PhysicalOperator*> operators,
        PhysicalResultCollector* collector) {
    Pipeline p;
    p.source    = source;
    p.operators = std::move(operators);
    p.sink      = collector;

    ClientContext ctx;
    PipelineExecutor pe(ctx, p);
    pe.Execute();
    return collector->TakeResult();
}

static void TestPipelineExecutorBasic() {
    // ConstantScan (3 rows) → ResultCollector → expect 3 rows.
    std::vector<int64_t> vals = {1, 2, 3};
    PhysicalConstantScan scan(vals);
    PhysicalResultCollector collector({"val"}, {TypeId::INT64});

    auto result = RunManualPipeline(&scan, {}, &collector);
    assert(result.success);
    assert(result.RowCount() == 3);
    assert(result.chunks[0].columns[0].int_data[0] == 1);
    assert(result.chunks[0].columns[0].int_data[1] == 2);
    assert(result.chunks[0].columns[0].int_data[2] == 3);
    std::cout << "TestPipelineExecutorBasic: PASS\n";
}

static void TestPipelineExecutorOperatorChain() {
    // ConstantScan → IdentityOperator → ResultCollector → same 3 rows.
    std::vector<int64_t> vals = {10, 20, 30};
    PhysicalConstantScan scan(vals);
    PhysicalIdentityOperator identity(TypeId::INT64);
    PhysicalResultCollector collector({"val"}, {TypeId::INT64});

    auto result = RunManualPipeline(&scan, {&identity}, &collector);
    assert(result.RowCount() == 3);
    assert(result.chunks[0].columns[0].int_data[0] == 10);
    assert(result.chunks[0].columns[0].int_data[2] == 30);
    std::cout << "TestPipelineExecutorOperatorChain: PASS\n";
}

static void TestPipelineExecutorNEEDSMOREINPUT() {
    // BufferTwo operator buffers 2 chunks before emitting.
    // Source emits in batches of 1 row.
    std::vector<int64_t> vals = {5, 6}; // 2 values, batch=1 → 2 source chunks
    PhysicalConstantScan scan(vals, /*batch_size=*/1);
    PhysicalBufferTwo buf;
    PhysicalResultCollector collector({"val"}, {TypeId::INT64});

    auto result = RunManualPipeline(&scan, {&buf}, &collector);
    assert(result.RowCount() == 2); // both values arrive in the combined chunk
    std::cout << "TestPipelineExecutorNEEDSMOREINPUT: PASS\n";
}

static void TestPipelineExecutorHAVEMORE_OUTPUT() {
    // DoubleOutput returns 2 output chunks per input chunk.
    // Source emits [7, 8] in one batch → DoubleOutput emits [7,8] twice → 4 total rows.
    std::vector<int64_t> vals = {7, 8};
    PhysicalConstantScan scan(vals);
    PhysicalDoubleOutput dbl;
    PhysicalResultCollector collector({"val"}, {TypeId::INT64});

    auto result = RunManualPipeline(&scan, {&dbl}, &collector);
    assert(result.RowCount() == 4); // 2 rows duplicated
    std::cout << "TestPipelineExecutorHAVE_MORE_OUTPUT: PASS\n";
}

static void TestPipelineExecutorEmptySource() {
    // Source with 0 rows → 0 result rows.
    PhysicalConstantScan scan({});
    PhysicalResultCollector collector({"val"}, {TypeId::INT64});

    auto result = RunManualPipeline(&scan, {}, &collector);
    assert(result.RowCount() == 0);
    std::cout << "TestPipelineExecutorEmptySource: PASS\n";
}

static void TestPipelineExecutorLargeSource() {
    // More than STANDARD_VECTOR_SIZE rows split across multiple source chunks.
    std::vector<int64_t> vals;
    for (int i = 0; i < 2500; ++i) vals.push_back(i);
    PhysicalConstantScan scan(vals); // batches of STANDARD_VECTOR_SIZE

    PhysicalResultCollector collector({"val"}, {TypeId::INT64});
    auto result = RunManualPipeline(&scan, {}, &collector);
    assert(result.RowCount() == 2500);
    std::cout << "TestPipelineExecutorLargeSource: PASS\n";
}

static void TestExecutorInitAndRun() {
    // Use the Executor class (which builds pipelines automatically).
    auto scan = std::make_unique<PhysicalConstantScan>(
        std::vector<int64_t>{100, 200, 300});

    ClientContext ctx;
    Executor exec(ctx);
    exec.Initialize(std::move(scan));
    exec.Execute();
    auto result = exec.GetResult();

    assert(result.RowCount() == 3);
    assert(result.column_names[0] == "val");
    std::cout << "TestExecutorInitAndRun: PASS\n";
}

static void TestQueryResultRowCount() {
    QueryResult qr;
    DataChunk c1; c1.Initialize({TypeId::INT64}); c1.count = 5;
    DataChunk c2; c2.Initialize({TypeId::INT64}); c2.count = 3;
    qr.chunks.push_back(std::move(c1));
    qr.chunks.push_back(std::move(c2));
    assert(qr.RowCount() == 8);
    std::cout << "TestQueryResultRowCount: PASS\n";
}

void RunPipelineExecutorTests() {
    TestPipelineExecutorBasic();
    TestPipelineExecutorOperatorChain();
    TestPipelineExecutorNEEDSMOREINPUT();
    TestPipelineExecutorHAVEMORE_OUTPUT();
    TestPipelineExecutorEmptySource();
    TestPipelineExecutorLargeSource();
    TestExecutorInitAndRun();
    TestQueryResultRowCount();
    std::cout << "\nAll PipelineExecutor tests passed.\n";
}
