#include "cppcoldb/common/types/data_chunk.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::common {

void DataVector::Reset(TypeId, std::size_t) { CPPCOLDB_NOT_IMPLEMENTED(); }

void DataChunk::Reset() { CPPCOLDB_NOT_IMPLEMENTED(); }

void DataChunk::Initialize(const std::vector<TypeId>&) { CPPCOLDB_NOT_IMPLEMENTED(); }

void DataVectorAppend(DataVector&, const DataVector&, std::size_t) { CPPCOLDB_NOT_IMPLEMENTED(); }

void DataVectorFill(DataVector&, const Value&, std::size_t) { CPPCOLDB_NOT_IMPLEMENTED(); }

void DataVectorAppendValue(DataVector&, const Value&) { CPPCOLDB_NOT_IMPLEMENTED(); }

void DataChunkSlice(DataChunk&, const DataChunk&, const std::vector<std::uint32_t>&) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::common
