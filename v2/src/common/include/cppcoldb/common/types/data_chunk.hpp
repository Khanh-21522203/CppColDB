#pragma once
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cppcoldb/common/constants.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/common/types/value.hpp"

namespace cppcoldb::common {

// A column of up to STANDARD_VECTOR_SIZE values of a single TypeId.
// validity[i] == true means row i is NOT NULL.
struct DataVector {
    TypeId type  = TypeId::INVALID;
    std::size_t count = 0;

    std::vector<std::int64_t> int_data;   // INT8/16/32/64, BOOLEAN (0/1)
    std::vector<double>       float_data; // FLOAT32/64
    std::vector<std::string>  str_data;   // VARCHAR

    std::bitset<STANDARD_VECTOR_SIZE> validity;

    void Reset(TypeId t, std::size_t n);
    void SetNull(std::size_t i)      { validity.reset(i); }
    bool IsNull(std::size_t i) const { return !validity[i]; }
};

// A batch of rows: one DataVector per column, all with the same count.
struct DataChunk {
    std::vector<DataVector> columns;
    std::size_t count = 0;

    void Reset();
    void Initialize(const std::vector<TypeId>& types);
    std::size_t ColumnCount() const { return columns.size(); }
};

void DataVectorAppend(DataVector& dst, const DataVector& src, std::size_t src_idx);
void DataVectorFill(DataVector& vec, const Value& val, std::size_t count);
void DataVectorAppendValue(DataVector& dst, const Value& val);
void DataChunkSlice(DataChunk& dst, const DataChunk& src,
                    const std::vector<std::uint32_t>& row_indices);

} // namespace cppcoldb::common
