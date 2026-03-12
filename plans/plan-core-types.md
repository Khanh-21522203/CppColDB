# Feature: Core Types

## 1. Purpose

Core Types is the foundational header-only layer that defines every primitive type used across the entire CppColDB system. All other modules depend on it; it depends on nothing beyond the C++20 standard library. Without a shared types layer, every subsystem would define its own `TypeId`, `BlockId`, or `Value`, leading to incompatible definitions and implicit conversions. By defining them once here, the compiler enforces type safety across the entire codebase.

## 2. Responsibilities

- Define `TypeId` enum: INT8, INT16, INT32, INT64, FLOAT32, FLOAT64, BOOLEAN, VARCHAR, INVALID
- Define `Value`: a runtime-typed scalar holding one SQL value of any supported type
- Define `DataVector`: a column of up to 1024 values of a fixed type with a validity bitmask for NULLs
- Define `DataChunk`: a batch of rows as a vector of `DataVector` columns plus a row count
- Define `BlockId` (uint64_t): opaque identifier for a page in the BufferManager's BlockFile
- Define `RowGroupId` (uint32_t): identifies a RowGroup within a table
- Define `TransactionId` (uint64_t): monotonically assigned per transaction
- Define `timestamp_t` (int64_t): microseconds since epoch, used for MVCC commit/start times
- Define `row_t` (uint64_t): logical row identifier combining RowGroupId and offset within it
- Provide `Value` comparison, hashing, and string conversion helpers
- Define `STANDARD_VECTOR_SIZE` constant (1024): the maximum rows per DataChunk

## 3. Non-Responsibilities

- No I/O or disk access
- No network calls
- No business logic (query planning, execution, storage)
- No configuration loading or logging
- No memory management beyond inline buffers

## 4. Architecture Design

Core Types sits at the bottom of the dependency graph. Every other module imports from it; it imports nothing.

```
+------------------------------------------------------------+
|                   All CppColDB Modules                     |
|  parser | binder | optimizer | executor | storage | txn    |
+----------------------------+-------------------------------+
                             |
                    #include "common/types.hpp"
                             |
              +--------------+--------------+
              |      src/common/types.hpp   |
              |  TypeId   Value   DataVector |
              |  DataChunk  BlockId  row_t  |
              |  TransactionId  timestamp_t |
              +------------------------------+
```

**DataVector layout**: fixed-size array of `STANDARD_VECTOR_SIZE` typed values plus a `std::bitset<1024>` validity mask. The array is allocated inline when `DataVector` owns its data, or holds a pointer when referencing a compressed block's decompressed buffer.

**row_t encoding**: upper 32 bits = RowGroupId, lower 32 bits = offset within that RowGroup. Allows addressing up to 2^32 RowGroups each with up to 2^32 rows.

**Value variant**: `std::variant<int64_t, double, bool, std::string>` with a `TypeId` tag. NULL is represented as `std::nullopt` in `std::optional<Value>`.

## 5. Core Data Structures (C++)

```cpp
// src/common/types.hpp

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
using BlockId   = uint64_t;
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

// Returns the fixed byte width for a numeric TypeId; 0 for VARCHAR.
size_t TypeSize(TypeId t);

// Returns a human-readable name for a TypeId.
std::string TypeName(TypeId t);

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
    static Value Null(TypeId t)         { Value val; val.type = t; return val; }

    bool IsNull() const { return type == TypeId::INVALID; }

    // Typed accessors (throw if type mismatch)
    int64_t     GetInt64()   const;
    double      GetFloat64() const;
    bool        GetBool()    const;
    const std::string& GetVarchar() const;

    std::string ToString() const;
    bool operator==(const Value& o) const;
    bool operator<(const Value& o) const;
};

// A column of up to STANDARD_VECTOR_SIZE values of a single TypeId.
// validity[i] == false means row i is NULL.
struct DataVector {
    TypeId type = TypeId::INVALID;
    size_t count = 0;                           // number of live values

    // Typed storage — only one is used depending on type.
    std::vector<int64_t>     int_data;          // INT8/16/32/64, BOOLEAN (0/1)
    std::vector<double>      float_data;        // FLOAT32/64
    std::vector<std::string> str_data;          // VARCHAR

    std::bitset<STANDARD_VECTOR_SIZE> validity; // bit i=1 means NOT NULL

    void Reset(TypeId t, size_t n);             // resize and clear
    void SetNull(size_t i);
    bool IsNull(size_t i) const { return !validity[i]; }
};

// A batch of rows: one DataVector per column, all with the same count.
struct DataChunk {
    std::vector<DataVector> columns; // indexed by column position
    size_t count = 0;                // rows in this batch (≤ STANDARD_VECTOR_SIZE)

    void Reset();
    void Initialize(const std::vector<TypeId>& types);
    size_t ColumnCount() const { return columns.size(); }
};

} // namespace cppcoldb
```

## 6. Public Interfaces

```cpp
// src/common/types.hpp — free functions

namespace cppcoldb {

// TypeId helpers
size_t      TypeSize(TypeId t);
std::string TypeName(TypeId t);
TypeId      TypeFromString(const std::string& s); // "INT32", "VARCHAR", etc.

// Value helpers
std::size_t ValueHash(const Value& v);
std::string ValueToString(const Value& v);

// DataVector helpers
void DataVectorAppend(DataVector& dst, const DataVector& src, size_t src_idx);
void DataVectorFill(DataVector& vec, const Value& val, size_t count);

// DataChunk helpers
void DataChunkSlice(DataChunk& dst, const DataChunk& src,
                    const std::vector<uint32_t>& row_indices);

} // namespace cppcoldb
```

## 7. Internal Algorithms

### TypeSize lookup
```
TypeSize(t):
  switch t:
    BOOLEAN, INT8            -> 1
    INT16                    -> 2
    INT32, FLOAT32           -> 4
    INT64, FLOAT64           -> 8
    VARCHAR                  -> 0  (variable)
    INVALID                  -> 0
```
Time complexity: O(1).

### Value comparison
```
Value::operator==(other):
  1. If types differ -> false
  2. Dispatch on type:
     INT64   -> get<int64_t>() == get<int64_t>()
     FLOAT64 -> get<double>()  == get<double>()
     BOOLEAN -> get<bool>()    == get<bool>()
     VARCHAR -> get<string>()  == get<string>()
  Time: O(n) for VARCHAR, O(1) otherwise.
```

### DataChunkSlice
```
DataChunkSlice(dst, src, row_indices):
  1. Initialize dst with same column types as src
  2. For each col c in src.columns:
     For each i in row_indices:
       append src.columns[c] row i -> dst.columns[c]
  3. dst.count = row_indices.size()
  Time: O(cols * selected_rows)
```

### row_t encoding
```
MakeRowId(rg, offset):
  return (uint64_t(rg) << 32) | uint64_t(offset)
  // Upper 32 bits: RowGroupId; lower 32 bits: row offset within RowGroup
  // Max rows: 2^32 RowGroups * 122,880 rows each ≈ 5.3e14 total rows
```

## 8. Persistence Model

Not applicable. Core types are not persisted directly. They appear as in-memory representations; persistence is handled by the storage layer which serializes/deserializes them.

## 9. Concurrency Model

All types in this module are value types or simple aggregates. They are safe for concurrent use when passed by value. `DataVector` and `DataChunk` are not thread-safe for concurrent mutation; callers must own them exclusively during modification. No locks or atomics needed at this layer.

## 10. Configuration

```cpp
// All constants are compile-time; no runtime configuration.
static constexpr size_t   STANDARD_VECTOR_SIZE = 1024;  // rows per DataChunk
static constexpr uint32_t ROW_GROUP_SIZE        = 122880; // rows per RowGroup
static constexpr BlockId  INVALID_BLOCK         = UINT64_MAX;
static constexpr TransactionId INVALID_TRANSACTION = 0;
```

## 11. Testing Strategy

- `TestTypeSize`: verify TypeSize returns correct byte widths for all TypeId values
- `TestTypeFromString`: round-trip TypeName → TypeFromString for each TypeId
- `TestValueInt`: create integer Value, call GetInt64(), verify equality and ToString()
- `TestValueFloat`: create float Value, verify GetFloat64(), less-than ordering
- `TestValueVarchar`: create varchar Value, verify string equality
- `TestValueNull`: create Null Value, verify IsNull(), GetInt64() throws
- `TestValueHash`: verify hash is consistent; two equal Values produce the same hash
- `TestDataVectorReset`: reset with INT64 type and 512 count, verify all null, correct size
- `TestDataVectorSetNull`: set specific indices null, verify via IsNull()
- `TestDataChunkInit`: initialize chunk with 3 columns, verify ColumnCount() == 3
- `TestDataChunkSlice`: create 10-row chunk, slice rows {1,3,5}, verify count == 3 and values
- `TestRowIdEncoding`: MakeRowId(5, 1000), verify RowIdGroup == 5 and RowIdOffset == 1000
- `TestRowIdMaxValues`: verify max RowGroupId and max offset do not overflow

## 12. Open Questions

None.
