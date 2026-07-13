#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace cppcoldb::common {

// SQL type identifiers.
enum class TypeId : std::uint8_t {
    INVALID = 0,
    BOOLEAN = 1,
    INT8    = 2,
    INT16   = 3,
    INT32   = 4,
    INT64   = 5,
    FLOAT32 = 6,
    FLOAT64 = 7,
    VARCHAR = 8,
};

// Returns the fixed byte width for a numeric TypeId; 0 for VARCHAR/INVALID.
std::size_t TypeSize(TypeId t);

// Returns a human-readable name for a TypeId (e.g. "INT64", "VARCHAR").
std::string TypeName(TypeId t);

// Parses a type name string back to TypeId; throws RuntimeError on unknown name.
TypeId TypeFromString(const std::string& s);

} // namespace cppcoldb::common
