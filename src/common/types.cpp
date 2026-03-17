#include "common/types.hpp"
#include "common/exception.hpp"

#include <functional>
#include <sstream>
#include <stdexcept>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// TypeId helpers
// ---------------------------------------------------------------------------

size_t TypeSize(TypeId t) {
    switch (t) {
        case TypeId::BOOLEAN: return 1;
        case TypeId::INT8:    return 1;
        case TypeId::INT16:   return 2;
        case TypeId::INT32:   return 4;
        case TypeId::FLOAT32: return 4;
        case TypeId::INT64:   return 8;
        case TypeId::FLOAT64: return 8;
        case TypeId::VARCHAR: return 0;
        case TypeId::INVALID: return 0;
    }
    return 0;
}

std::string TypeName(TypeId t) {
    switch (t) {
        case TypeId::BOOLEAN: return "BOOLEAN";
        case TypeId::INT8:    return "INT8";
        case TypeId::INT16:   return "INT16";
        case TypeId::INT32:   return "INT32";
        case TypeId::INT64:   return "INT64";
        case TypeId::FLOAT32: return "FLOAT32";
        case TypeId::FLOAT64: return "FLOAT64";
        case TypeId::VARCHAR: return "VARCHAR";
        case TypeId::INVALID: return "INVALID";
    }
    return "INVALID";
}

TypeId TypeFromString(const std::string& s) {
    if (s == "BOOLEAN") return TypeId::BOOLEAN;
    if (s == "INT8")    return TypeId::INT8;
    if (s == "INT16")   return TypeId::INT16;
    if (s == "INT32")   return TypeId::INT32;
    if (s == "INT64")   return TypeId::INT64;
    if (s == "FLOAT32") return TypeId::FLOAT32;
    if (s == "FLOAT64") return TypeId::FLOAT64;
    if (s == "VARCHAR") return TypeId::VARCHAR;
    throw RuntimeError("Unknown type name: " + s);
}

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

int64_t Value::GetInt64() const {
    if (type != TypeId::INT64 && type != TypeId::INT32 &&
        type != TypeId::INT16 && type != TypeId::INT8) {
        throw RuntimeError("Value::GetInt64 called on non-integer type");
    }
    return std::get<int64_t>(data);
}

double Value::GetFloat64() const {
    if (type != TypeId::FLOAT64 && type != TypeId::FLOAT32) {
        throw RuntimeError("Value::GetFloat64 called on non-float type");
    }
    return std::get<double>(data);
}

bool Value::GetBool() const {
    if (type != TypeId::BOOLEAN) {
        throw RuntimeError("Value::GetBool called on non-boolean type");
    }
    return std::get<bool>(data);
}

const std::string& Value::GetVarchar() const {
    if (type != TypeId::VARCHAR) {
        throw RuntimeError("Value::GetVarchar called on non-varchar type");
    }
    return std::get<std::string>(data);
}

std::string Value::ToString() const {
    if (IsNull()) return "NULL";
    switch (type) {
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:  return std::to_string(std::get<int64_t>(data));
        case TypeId::FLOAT32:
        case TypeId::FLOAT64: return std::to_string(std::get<double>(data));
        case TypeId::BOOLEAN: return std::get<bool>(data) ? "true" : "false";
        case TypeId::VARCHAR: return std::get<std::string>(data);
        case TypeId::INVALID: return "NULL";
    }
    return "NULL";
}

bool Value::operator==(const Value& o) const {
    if (type != o.type) return false;
    if (IsNull())       return false; // NULL != NULL per SQL semantics
    switch (type) {
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:   return std::get<int64_t>(data) == std::get<int64_t>(o.data);
        case TypeId::FLOAT32:
        case TypeId::FLOAT64: return std::get<double>(data)  == std::get<double>(o.data);
        case TypeId::BOOLEAN: return std::get<bool>(data)    == std::get<bool>(o.data);
        case TypeId::VARCHAR: return std::get<std::string>(data) == std::get<std::string>(o.data);
        case TypeId::INVALID: return false;
    }
    return false;
}

bool Value::operator<(const Value& o) const {
    if (type != o.type) return false;
    if (IsNull())       return false;
    switch (type) {
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:   return std::get<int64_t>(data) < std::get<int64_t>(o.data);
        case TypeId::FLOAT32:
        case TypeId::FLOAT64: return std::get<double>(data)  < std::get<double>(o.data);
        case TypeId::BOOLEAN: return std::get<bool>(data)    < std::get<bool>(o.data);
        case TypeId::VARCHAR: return std::get<std::string>(data) < std::get<std::string>(o.data);
        case TypeId::INVALID: return false;
    }
    return false;
}

std::size_t ValueHash(const Value& v) {
    if (v.IsNull()) return 0;
    switch (v.type) {
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:
            return std::hash<int64_t>{}(std::get<int64_t>(v.data));
        case TypeId::FLOAT32:
        case TypeId::FLOAT64:
            return std::hash<double>{}(std::get<double>(v.data));
        case TypeId::BOOLEAN:
            return std::hash<bool>{}(std::get<bool>(v.data));
        case TypeId::VARCHAR:
            return std::hash<std::string>{}(std::get<std::string>(v.data));
        case TypeId::INVALID:
            return 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// DataVector
// ---------------------------------------------------------------------------

void DataVector::Reset(TypeId t, size_t n) {
    type  = t;
    count = n;
    validity.reset(); // all NULL

    int_data.clear();
    float_data.clear();
    str_data.clear();

    switch (t) {
        case TypeId::BOOLEAN:
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:
            int_data.assign(n, 0);
            break;
        case TypeId::FLOAT32:
        case TypeId::FLOAT64:
            float_data.assign(n, 0.0);
            break;
        case TypeId::VARCHAR:
            str_data.assign(n, "");
            break;
        case TypeId::INVALID:
            break;
    }
}

// ---------------------------------------------------------------------------
// DataChunk
// ---------------------------------------------------------------------------

void DataChunk::Reset() {
    for (auto& col : columns) {
        col.Reset(col.type, 0);
    }
    count = 0;
}

void DataChunk::Initialize(const std::vector<TypeId>& types) {
    columns.resize(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
        columns[i].Reset(types[i], 0);
    }
    count = 0;
}

// ---------------------------------------------------------------------------
// DataVector helpers
// ---------------------------------------------------------------------------

void DataVectorAppend(DataVector& dst, const DataVector& src, size_t src_idx) {
    size_t dst_idx = dst.count;
    dst.count++;

    if (src.IsNull(src_idx)) {
        dst.SetNull(dst_idx);
        return;
    }

    dst.validity.set(dst_idx);

    switch (dst.type) {
        case TypeId::BOOLEAN:
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:
            if (dst.int_data.size() <= dst_idx)
                dst.int_data.resize(dst_idx + 1, 0);
            dst.int_data[dst_idx] = src.int_data[src_idx];
            break;
        case TypeId::FLOAT32:
        case TypeId::FLOAT64:
            if (dst.float_data.size() <= dst_idx)
                dst.float_data.resize(dst_idx + 1, 0.0);
            dst.float_data[dst_idx] = src.float_data[src_idx];
            break;
        case TypeId::VARCHAR:
            if (dst.str_data.size() <= dst_idx)
                dst.str_data.resize(dst_idx + 1, "");
            dst.str_data[dst_idx] = src.str_data[src_idx];
            break;
        case TypeId::INVALID:
            break;
    }
}

void DataVectorFill(DataVector& vec, const Value& val, size_t count) {
    vec.Reset(val.type, count);
    if (val.IsNull()) return; // all NULL already

    for (size_t i = 0; i < count; ++i) {
        vec.validity.set(i);
    }

    switch (val.type) {
        case TypeId::BOOLEAN:
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:
            vec.int_data.assign(count, std::get<int64_t>(val.data));
            break;
        case TypeId::FLOAT32:
        case TypeId::FLOAT64:
            vec.float_data.assign(count, std::get<double>(val.data));
            break;
        case TypeId::VARCHAR:
            vec.str_data.assign(count, std::get<std::string>(val.data));
            break;
        case TypeId::INVALID:
            break;
    }
}

// ---------------------------------------------------------------------------
// DataChunk helpers
// ---------------------------------------------------------------------------

void DataChunkSlice(DataChunk& dst, const DataChunk& src,
                    const std::vector<uint32_t>& row_indices) {
    std::vector<TypeId> types;
    types.reserve(src.ColumnCount());
    for (const auto& col : src.columns) {
        types.push_back(col.type);
    }
    dst.Initialize(types);

    for (size_t c = 0; c < src.ColumnCount(); ++c) {
        dst.columns[c].Reset(src.columns[c].type, 0);
        // Pre-allocate storage
        switch (src.columns[c].type) {
            case TypeId::BOOLEAN:
            case TypeId::INT8:
            case TypeId::INT16:
            case TypeId::INT32:
            case TypeId::INT64:
                dst.columns[c].int_data.resize(row_indices.size(), 0);
                break;
            case TypeId::FLOAT32:
            case TypeId::FLOAT64:
                dst.columns[c].float_data.resize(row_indices.size(), 0.0);
                break;
            case TypeId::VARCHAR:
                dst.columns[c].str_data.resize(row_indices.size(), "");
                break;
            case TypeId::INVALID:
                break;
        }
        for (uint32_t idx : row_indices) {
            DataVectorAppend(dst.columns[c], src.columns[c], idx);
        }
    }
    dst.count = row_indices.size();
}

void DataVectorAppendValue(DataVector& dst, const Value& val) {
    size_t i = dst.count++;
    if (val.IsNull()) {
        dst.SetNull(i);
        return;
    }
    dst.validity.set(i);
    switch (dst.type) {
        case TypeId::BOOLEAN:
            if (dst.int_data.size() <= i) dst.int_data.resize(i + 1, 0);
            dst.int_data[i] = std::get<bool>(val.data) ? 1 : 0;
            break;
        case TypeId::INT8:
        case TypeId::INT16:
        case TypeId::INT32:
        case TypeId::INT64:
            if (dst.int_data.size() <= i) dst.int_data.resize(i + 1, 0);
            dst.int_data[i] = std::get<int64_t>(val.data);
            break;
        case TypeId::FLOAT32:
        case TypeId::FLOAT64:
            if (dst.float_data.size() <= i) dst.float_data.resize(i + 1, 0.0);
            dst.float_data[i] = std::get<double>(val.data);
            break;
        case TypeId::VARCHAR:
            if (dst.str_data.size() <= i) dst.str_data.resize(i + 1, "");
            dst.str_data[i] = std::get<std::string>(val.data);
            break;
        case TypeId::INVALID:
            dst.SetNull(i);
            break;
    }
}

} // namespace cppcoldb
