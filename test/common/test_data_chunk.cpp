#include "common/types.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace cppcoldb;

static void TestDataChunkInit() {
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64, TypeId::VARCHAR, TypeId::FLOAT64});
    assert(chunk.ColumnCount() == 3);
    assert(chunk.count == 0);
    assert(chunk.columns[0].type == TypeId::INT64);
    assert(chunk.columns[1].type == TypeId::VARCHAR);
    assert(chunk.columns[2].type == TypeId::FLOAT64);
    std::cout << "TestDataChunkInit: PASS\n";
}

static void TestDataChunkReset() {
    DataChunk chunk;
    chunk.Initialize({TypeId::INT64, TypeId::BOOLEAN});

    // Manually populate
    chunk.columns[0].Reset(TypeId::INT64, 5);
    chunk.columns[0].int_data = {1, 2, 3, 4, 5};
    for (size_t i = 0; i < 5; ++i) chunk.columns[0].validity.set(i);
    chunk.count = 5;

    chunk.Reset();
    assert(chunk.count == 0);
    assert(chunk.columns[0].count == 0);
    std::cout << "TestDataChunkReset: PASS\n";
}

static void TestDataChunkSlice() {
    // Build a source chunk with 10 rows, one INT64 column: values 0..9
    DataChunk src;
    src.Initialize({TypeId::INT64});
    src.columns[0].Reset(TypeId::INT64, 10);
    for (size_t i = 0; i < 10; ++i) {
        src.columns[0].int_data[i] = static_cast<int64_t>(i);
        src.columns[0].validity.set(i);
    }
    src.count = 10;

    // Slice rows 1, 3, 5
    DataChunk dst;
    DataChunkSlice(dst, src, {1, 3, 5});

    assert(dst.count == 3);
    assert(dst.ColumnCount() == 1);
    assert(!dst.columns[0].IsNull(0));
    assert(!dst.columns[0].IsNull(1));
    assert(!dst.columns[0].IsNull(2));
    assert(dst.columns[0].int_data[0] == 1);
    assert(dst.columns[0].int_data[1] == 3);
    assert(dst.columns[0].int_data[2] == 5);
    std::cout << "TestDataChunkSlice: PASS\n";
}

static void TestDataChunkSliceMultiColumn() {
    // Two columns: INT64 and VARCHAR
    DataChunk src;
    src.Initialize({TypeId::INT64, TypeId::VARCHAR});
    src.columns[0].Reset(TypeId::INT64, 4);
    src.columns[1].Reset(TypeId::VARCHAR, 4);
    for (size_t i = 0; i < 4; ++i) {
        src.columns[0].int_data[i] = static_cast<int64_t>(i * 10);
        src.columns[0].validity.set(i);
        src.columns[1].str_data[i] = "s" + std::to_string(i);
        src.columns[1].validity.set(i);
    }
    src.count = 4;

    DataChunk dst;
    DataChunkSlice(dst, src, {0, 2});

    assert(dst.count == 2);
    assert(dst.columns[0].int_data[0] == 0);
    assert(dst.columns[0].int_data[1] == 20);
    assert(dst.columns[1].str_data[0] == "s0");
    assert(dst.columns[1].str_data[1] == "s2");
    std::cout << "TestDataChunkSliceMultiColumn: PASS\n";
}

static void TestDataChunkSlicePreservesNull() {
    // Row 1 is NULL, row 2 is not null
    DataChunk src;
    src.Initialize({TypeId::INT64});
    src.columns[0].Reset(TypeId::INT64, 3);
    src.columns[0].int_data = {10, 20, 30};
    src.columns[0].validity.set(0); // row 0 not null
    // row 1 stays null
    src.columns[0].validity.set(2); // row 2 not null
    src.count = 3;

    DataChunk dst;
    DataChunkSlice(dst, src, {0, 1, 2});

    assert(!dst.columns[0].IsNull(0));
    assert(dst.columns[0].IsNull(1));
    assert(!dst.columns[0].IsNull(2));
    std::cout << "TestDataChunkSlicePreservesNull: PASS\n";
}

void RunDataChunkTests() {
    TestDataChunkInit();
    TestDataChunkReset();
    TestDataChunkSlice();
    TestDataChunkSliceMultiColumn();
    TestDataChunkSlicePreservesNull();

    std::cout << "\nAll test_data_chunk tests passed.\n";
}
