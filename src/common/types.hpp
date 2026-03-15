#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <optional>
#include <bitset>
#include <vector>
#include <stdexcept>

namespace cppcoldb {

// Maximum rows processed per DataChunk (vectorized batch size).
static constexpr size_t STANDARD_VECTOR_SIZE = 1024;

// Default RowGroup row capacity.
static constexpr uint32_t ROW_GROUP_SIZE = 122880; // 120 * 1024

// Opaque block identifier used by BufferManager and BlockFile.
using BlockId = uint64_t;
static constexpr BlockId INVALID_BLOCK = UINT64_MAX;

// Monotonically assigned transaction identifier.
using TransactionId = uint64_t;
static constexpr TransactionId INVALID_TRANSACTION = 0;

// Microseconds since epoch; used for MVCC snapshot timestamps.
using timestamp_t = int64_t;
static constexpr timestamp_t INVALID_TIMESTAMP = -1;

// Identifies a RowGroup within a table (index in the RowGroup list).
using RowGroupId = uint32_t;

// Logical row identifier: upper 32 bits = RowGroupId, lower 32 bits = row offset.
using row_t = uint64_t;

inline row_t MakeRowId(RowGroupId rg, uint32_t offset) {
    return (static_cast<uint64_t>(rg) << 32) | offset;
}
inline RowGroupId RowIdGroup(row_t r)  { return static_cast<RowGroupId>(r >> 32); }
inline uint32_t   RowIdOffset(row_t r) { return static_cast<uint32_t>(r); }

// SQL type identifiers.
enum class TypeId : uint8_t {
    INVALID  = 0,
    BOOLEAN  = 1,
    INT8     = 2,
    INT16    = 3,
    INT32    = 4,
    INT64    = 5,
    FLOAT32  = 6,
    FLOAT64  = 7,
    VARCHAR  = 8,
};

// Returns the fixed byte width for a numeric TypeId; 0 for VARCHAR/INVALID.
size_t TypeSize(TypeId t);

// Returns a human-readable name for a TypeId (e.g. "INT64", "VARCHAR").
std::string TypeName(TypeId t);

// Parses a type name string back to TypeId; throws RuntimeError on unknown name.
TypeId TypeFromString(const std::string& s);

// Runtime-typed scalar SQL value.
// NULL is represented as std::nullopt at call sites using std::optional<Value>.
struct Value {
    TypeId type = TypeId::INVALID;
    std::variant<int64_t, double, bool, std::string> data;

    // Factory helpers
    static Value Integer(int64_t v)     { return {TypeId::INT64,   v}; }
    static Value Float(double v)        { return {TypeId::FLOAT64, v}; }
    static Value Boolean(bool v)        { return {TypeId::BOOLEAN, v}; }
    static Value Varchar(std::string v) { return {TypeId::VARCHAR, std::move(v)}; }
    static Value Null(TypeId t = TypeId::INVALID) { Value val; val.type = t; return val; }

    // A Value is NULL when its type is INVALID (no data).
    bool IsNull() const { return type == TypeId::INVALID; }

    // Typed accessors — throw RuntimeError on type mismatch.
    int64_t            GetInt64()   const;
    double             GetFloat64() const;
    bool               GetBool()    const;
    const std::string& GetVarchar() const;

    std::string ToString() const;
    bool operator==(const Value& o) const;
    bool operator<(const Value& o) const;
};

// Hash a Value (for use in hash tables).
std::size_t ValueHash(const Value& v);

// A column of up to STANDARD_VECTOR_SIZE values of a single TypeId.
// validity[i] == true means row i is NOT NULL.
struct DataVector {
    TypeId type  = TypeId::INVALID;
    size_t count = 0; // number of live values

    // Typed storage — only one is used depending on type.
    std::vector<int64_t>     int_data;   // INT8/16/32/64, BOOLEAN (0/1)
    std::vector<double>      float_data; // FLOAT32/64
    std::vector<std::string> str_data;   // VARCHAR

    std::bitset<STANDARD_VECTOR_SIZE> validity; // bit i=1 means NOT NULL

    // Resize to n rows of type t; all values set to NULL.
    void Reset(TypeId t, size_t n);

    void SetNull(size_t i)       { validity.reset(i); }
    bool IsNull(size_t i) const  { return !validity[i]; }
};

// A batch of rows: one DataVector per column, all with the same count.
struct DataChunk {
    std::vector<DataVector> columns; // indexed by column position
    size_t count = 0;                // rows in this batch (≤ STANDARD_VECTOR_SIZE)

    void Reset();
    void Initialize(const std::vector<TypeId>& types);
    size_t ColumnCount() const { return columns.size(); }
};

// Append a single row (src_idx) from src into dst at dst.count.
void DataVectorAppend(DataVector& dst, const DataVector& src, size_t src_idx);

// Fill dst with count copies of val.
void DataVectorFill(DataVector& vec, const Value& val, size_t count);

// Build dst as a subset of src rows given by row_indices.
void DataChunkSlice(DataChunk& dst, const DataChunk& src,
                    const std::vector<uint32_t>& row_indices);

} // namespace cppcoldb
