#include "common/types.hpp"
#include "common/exception.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace cppcoldb;

// ---------------------------------------------------------------------------
// TypeId helpers
// ---------------------------------------------------------------------------

static void TestTypeSize() {
    assert(TypeSize(TypeId::BOOLEAN) == 1);
    assert(TypeSize(TypeId::INT8)    == 1);
    assert(TypeSize(TypeId::INT16)   == 2);
    assert(TypeSize(TypeId::INT32)   == 4);
    assert(TypeSize(TypeId::INT64)   == 8);
    assert(TypeSize(TypeId::FLOAT32) == 4);
    assert(TypeSize(TypeId::FLOAT64) == 8);
    assert(TypeSize(TypeId::VARCHAR) == 0);
    assert(TypeSize(TypeId::INVALID) == 0);
    std::cout << "TestTypeSize: PASS\n";
}

static void TestTypeFromString() {
    // Round-trip: TypeName -> TypeFromString
    for (auto t : {TypeId::BOOLEAN, TypeId::INT8, TypeId::INT16, TypeId::INT32,
                   TypeId::INT64, TypeId::FLOAT32, TypeId::FLOAT64, TypeId::VARCHAR}) {
        assert(TypeFromString(TypeName(t)) == t);
    }

    // Unknown name throws
    bool threw = false;
    try { TypeFromString("NOTYPE"); } catch (const RuntimeError&) { threw = true; }
    assert(threw);

    std::cout << "TestTypeFromString: PASS\n";
}

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

static void TestValueInt() {
    auto v = Value::Integer(42);
    assert(v.type == TypeId::INT64);
    assert(v.GetInt64() == 42);
    assert(!v.IsNull());
    assert(v.ToString() == "42");
    assert(v == Value::Integer(42));
    std::cout << "TestValueInt: PASS\n";
}

static void TestValueFloat() {
    auto v = Value::Float(3.14);
    assert(v.type == TypeId::FLOAT64);
    assert(v.GetFloat64() == 3.14);
    assert(v < Value::Float(9.99));
    assert(!(Value::Float(9.99) < v));
    std::cout << "TestValueFloat: PASS\n";
}

static void TestValueVarchar() {
    auto v = Value::Varchar("hello");
    assert(v.type == TypeId::VARCHAR);
    assert(v.GetVarchar() == "hello");
    assert(v == Value::Varchar("hello"));
    assert(!(v == Value::Varchar("world")));
    std::cout << "TestValueVarchar: PASS\n";
}

static void TestValueNull() {
    auto v = Value::Null();
    assert(v.IsNull());
    assert(v.ToString() == "NULL");

    // Accessing a null value should throw
    bool threw = false;
    try { v.GetInt64(); } catch (const RuntimeError&) { threw = true; }
    assert(threw);

    // Two NULLs are not equal (SQL semantics)
    assert(!(v == v));
    std::cout << "TestValueNull: PASS\n";
}

static void TestValueHash() {
    auto v1 = Value::Integer(100);
    auto v2 = Value::Integer(100);
    auto v3 = Value::Integer(200);

    assert(ValueHash(v1) == ValueHash(v2));
    assert(ValueHash(v1) != ValueHash(v3)); // not guaranteed but very likely
    assert(ValueHash(Value::Null()) == 0);
    std::cout << "TestValueHash: PASS\n";
}

// ---------------------------------------------------------------------------
// row_t encoding
// ---------------------------------------------------------------------------

static void TestRowIdEncoding() {
    row_t r = MakeRowId(5, 1000);
    assert(RowIdGroup(r) == 5);
    assert(RowIdOffset(r) == 1000);
    std::cout << "TestRowIdEncoding: PASS\n";
}

static void TestRowIdMaxValues() {
    RowGroupId max_rg     = UINT32_MAX;
    uint32_t   max_offset = UINT32_MAX;
    row_t r = MakeRowId(max_rg, max_offset);
    assert(RowIdGroup(r)  == max_rg);
    assert(RowIdOffset(r) == max_offset);
    std::cout << "TestRowIdMaxValues: PASS\n";
}

// ---------------------------------------------------------------------------
// DataVector
// ---------------------------------------------------------------------------

static void TestDataVectorReset() {
    DataVector dv;
    dv.Reset(TypeId::INT64, 512);
    assert(dv.type  == TypeId::INT64);
    assert(dv.count == 512);
    assert(dv.int_data.size() == 512);
    // All null after reset
    for (size_t i = 0; i < 512; ++i) {
        assert(dv.IsNull(i));
    }
    std::cout << "TestDataVectorReset: PASS\n";
}

static void TestDataVectorSetNull() {
    DataVector dv;
    dv.Reset(TypeId::INT64, 10);
    dv.validity.set(3); // row 3 is NOT null
    dv.validity.set(7); // row 7 is NOT null

    assert(dv.IsNull(0));
    assert(!dv.IsNull(3));
    assert(!dv.IsNull(7));
    assert(dv.IsNull(9));

    dv.SetNull(3);
    assert(dv.IsNull(3));
    std::cout << "TestDataVectorSetNull: PASS\n";
}

static void TestDataVectorFill() {
    DataVector dv;
    DataVectorFill(dv, Value::Integer(99), 5);
    assert(dv.count == 5);
    for (size_t i = 0; i < 5; ++i) {
        assert(!dv.IsNull(i));
        assert(dv.int_data[i] == 99);
    }
    std::cout << "TestDataVectorFill: PASS\n";
}

void RunTypesTests() {
    TestTypeSize();
    TestTypeFromString();
    TestValueInt();
    TestValueFloat();
    TestValueVarchar();
    TestValueNull();
    TestValueHash();
    TestRowIdEncoding();
    TestRowIdMaxValues();
    TestDataVectorReset();
    TestDataVectorSetNull();
    TestDataVectorFill();

    std::cout << "\nAll test_types tests passed.\n";
}
