#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

#include "cppcoldb/common/types/type_id.hpp"

namespace cppcoldb::common {

// Runtime-typed scalar SQL value. NULL is represented as type == INVALID.
struct Value {
    TypeId type = TypeId::INVALID;
    std::variant<std::int64_t, double, bool, std::string> data;

    static Value Integer(std::int64_t v) { return {TypeId::INT64, v}; }
    static Value Float(double v)         { return {TypeId::FLOAT64, v}; }
    static Value Boolean(bool v)         { return {TypeId::BOOLEAN, v}; }
    static Value Varchar(std::string v)  { return {TypeId::VARCHAR, std::move(v)}; }
    static Value Null(TypeId t = TypeId::INVALID) { Value val; val.type = t; return val; }

    bool IsNull() const { return type == TypeId::INVALID; }

    // Typed accessors — throw RuntimeError on type mismatch.
    std::int64_t       GetInt64() const;
    double             GetFloat64() const;
    bool               GetBool() const;
    const std::string& GetVarchar() const;

    std::string ToString() const;
    bool operator==(const Value& o) const;
    bool operator<(const Value& o) const;
};

// Hash a Value (for use in hash tables).
std::size_t ValueHash(const Value& v);

} // namespace cppcoldb::common
