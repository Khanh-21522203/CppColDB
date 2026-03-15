#include "storage/column/column_chunk.hpp"
#include "storage/buffer_manager.hpp"
#include "common/types.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace cppcoldb;

// ---- helpers ---------------------------------------------------------------

static DataVector MakeIntVec(const std::vector<int64_t>& vals) {
    DataVector v;
    v.Reset(TypeId::INT64, vals.size());
    v.int_data = vals;
    for (size_t i = 0; i < vals.size(); ++i) v.validity.set(i);
    v.count = vals.size();
    return v;
}

static DataVector MakeStrVec(const std::vector<std::string>& vals) {
    DataVector v;
    v.Reset(TypeId::VARCHAR, vals.size());
    v.str_data = vals;
    for (size_t i = 0; i < vals.size(); ++i) v.validity.set(i);
    v.count = vals.size();
    return v;
}

// ---- tests -----------------------------------------------------------------

static void TestColumnChunkAppendScan() {
    BufferManager bm; // in-memory mode
    ColumnChunk cc(TypeId::INT64, bm);

    std::vector<int64_t> vals;
    for (int i = 0; i < 100; ++i) vals.push_back(i * 3);
    DataVector dv = MakeIntVec(vals);
    cc.AppendFromVector(dv, dv.count);

    assert(cc.RowCount() == 100);

    ColumnScanState state = cc.MakeScanState(0);
    DataVector out;
    size_t n = cc.Scan(state, 100, out);

    assert(n == 100);
    for (int i = 0; i < 100; ++i) {
        assert(!out.IsNull(i));
        assert(out.int_data[i] == i * 3);
    }
    std::cout << "TestColumnChunkAppendScan: PASS\n";
}

static void TestColumnChunkPartialScan() {
    BufferManager bm;
    ColumnChunk cc(TypeId::INT64, bm);

    std::vector<int64_t> vals;
    for (int i = 0; i < 200; ++i) vals.push_back(i);
    cc.AppendFromVector(MakeIntVec(vals), 200);

    // Scan starting at row 50
    ColumnScanState state = cc.MakeScanState(50);
    DataVector out;
    size_t n = cc.Scan(state, 100, out);

    assert(n == 100);
    for (int i = 0; i < 100; ++i) {
        assert(out.int_data[i] == 50 + i);
    }
    std::cout << "TestColumnChunkPartialScan: PASS\n";
}

static void TestColumnChunkFlushAndScan() {
    BufferManager bm;
    ColumnChunk cc(TypeId::INT64, bm);

    std::vector<int64_t> vals;
    for (int i = 0; i < 50; ++i) vals.push_back(i + 1000);
    cc.AppendFromVector(MakeIntVec(vals), 50);

    assert(cc.SegmentRowCount() == 0);
    cc.Flush();
    assert(cc.SegmentRowCount() == 50);
    assert(cc.RowCount() == 50);

    ColumnScanState state = cc.MakeScanState(0);
    DataVector out;
    size_t n = cc.Scan(state, 50, out);

    assert(n == 50);
    for (int i = 0; i < 50; ++i) {
        assert(!out.IsNull(i));
        assert(out.int_data[i] == i + 1000);
    }
    std::cout << "TestColumnChunkFlushAndScan: PASS\n";
}

static void TestColumnChunkMultiFlushScan() {
    // Two flushes → two segments; scan across boundary.
    BufferManager bm;
    ColumnChunk cc(TypeId::INT64, bm);

    std::vector<int64_t> batch1, batch2;
    for (int i = 0;   i < 100; ++i) batch1.push_back(i);
    for (int i = 100; i < 200; ++i) batch2.push_back(i);

    cc.AppendFromVector(MakeIntVec(batch1), 100);
    cc.Flush();
    cc.AppendFromVector(MakeIntVec(batch2), 100);
    cc.Flush();

    assert(cc.SegmentRowCount() == 200);

    // Scan all 200 rows
    ColumnScanState state = cc.MakeScanState(0);
    DataVector out;
    size_t n = cc.Scan(state, 256, out);

    assert(n == 200);
    for (int i = 0; i < 200; ++i) {
        assert(out.int_data[i] == i);
    }
    std::cout << "TestColumnChunkMultiFlushScan: PASS\n";
}

static void TestColumnChunkNullHandling() {
    BufferManager bm;
    ColumnChunk cc(TypeId::INT64, bm);

    // Rows: [1 (non-null), null, 3 (non-null), null, 5 (non-null)]
    DataVector dv;
    dv.Reset(TypeId::INT64, 5);
    dv.int_data = {1, 0, 3, 0, 5};
    dv.validity.set(0);
    dv.validity.set(2);
    dv.validity.set(4);
    dv.count = 5;

    cc.AppendFromVector(dv, 5);

    ColumnScanState state = cc.MakeScanState(0);
    DataVector out;
    cc.Scan(state, 5, out);

    assert(!out.IsNull(0)); assert(out.int_data[0] == 1);
    assert( out.IsNull(1));
    assert(!out.IsNull(2)); assert(out.int_data[2] == 3);
    assert( out.IsNull(3));
    assert(!out.IsNull(4)); assert(out.int_data[4] == 5);
    std::cout << "TestColumnChunkNullHandling: PASS\n";
}

static void TestColumnChunkFlushNullHandling() {
    // Same null pattern but through a Flush (compression round-trip).
    BufferManager bm;
    ColumnChunk cc(TypeId::INT64, bm);

    DataVector dv;
    dv.Reset(TypeId::INT64, 4);
    dv.int_data = {10, 0, 30, 0};
    dv.validity.set(0);
    dv.validity.set(2);
    dv.count = 4;

    cc.AppendFromVector(dv, 4);
    cc.Flush();

    ColumnScanState state = cc.MakeScanState(0);
    DataVector out;
    cc.Scan(state, 4, out);

    assert(!out.IsNull(0)); assert(out.int_data[0] == 10);
    assert( out.IsNull(1));
    assert(!out.IsNull(2)); assert(out.int_data[2] == 30);
    assert( out.IsNull(3));
    std::cout << "TestColumnChunkFlushNullHandling: PASS\n";
}

static void TestColumnChunkVarchar() {
    BufferManager bm;
    ColumnChunk cc(TypeId::VARCHAR, bm);

    std::vector<std::string> vals = {"alpha", "beta", "gamma", "delta"};
    cc.AppendFromVector(MakeStrVec(vals), vals.size());
    cc.Flush();

    ColumnScanState state = cc.MakeScanState(0);
    DataVector out;
    cc.Scan(state, 4, out);

    assert(out.str_data[0] == "alpha");
    assert(out.str_data[1] == "beta");
    assert(out.str_data[2] == "gamma");
    assert(out.str_data[3] == "delta");
    std::cout << "TestColumnChunkVarchar: PASS\n";
}

static void TestColumnChunkTruncatePending() {
    BufferManager bm;
    ColumnChunk cc(TypeId::INT64, bm);

    std::vector<int64_t> vals = {1, 2, 3, 4, 5};
    cc.AppendFromVector(MakeIntVec(vals), 5);
    assert(cc.RowCount() == 5);

    cc.TruncatePending(3);
    assert(cc.RowCount() == 3);

    ColumnScanState state = cc.MakeScanState(0);
    DataVector out;
    cc.Scan(state, 3, out);

    assert(out.count == 3);
    assert(out.int_data[0] == 1);
    assert(out.int_data[1] == 2);
    assert(out.int_data[2] == 3);
    std::cout << "TestColumnChunkTruncatePending: PASS\n";
}

void RunColumnChunkTests() {
    TestColumnChunkAppendScan();
    TestColumnChunkPartialScan();
    TestColumnChunkFlushAndScan();
    TestColumnChunkMultiFlushScan();
    TestColumnChunkNullHandling();
    TestColumnChunkFlushNullHandling();
    TestColumnChunkVarchar();
    TestColumnChunkTruncatePending();
    std::cout << "\nAll ColumnChunk tests passed.\n";
}
