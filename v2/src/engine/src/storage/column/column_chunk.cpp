#include "cppcoldb/engine/storage/column/column_chunk.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::storage {

ColumnChunk::ColumnChunk(common::TypeId type, IBufferManager& bm) : type_(type), bm_(bm) {}

ColumnScanState ColumnChunk::MakeScanState(std::size_t /*row_offset*/) const { CPPCOLDB_NOT_IMPLEMENTED(); }

std::size_t ColumnChunk::Scan(ColumnScanState& /*state*/, std::size_t /*count*/,
                               common::DataVector& /*output*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void ColumnChunk::Flush() { CPPCOLDB_NOT_IMPLEMENTED(); }

void ColumnChunk::AppendFromVector(const common::DataVector& /*vec*/, std::size_t /*count*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void ColumnChunk::TruncatePending(std::size_t /*new_count*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void ColumnChunk::WriteRow(std::uint32_t /*row_offset*/, const common::DataVector& /*src*/,
                            std::size_t /*src_idx*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void ColumnChunk::WriteRows(const std::vector<std::uint32_t>& /*row_offsets*/,
                             const common::DataVector& /*src*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void ColumnChunk::ScanRows(const std::vector<std::uint32_t>& /*offsets*/,
                            common::DataVector& /*output*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

std::size_t ColumnChunk::ZoneMapSkipRows(std::size_t /*row_offset*/, ScanPredicateOp /*op*/,
                                          const common::Value& /*bound*/) const { CPPCOLDB_NOT_IMPLEMENTED(); }

std::size_t ColumnChunk::RowCount() const { CPPCOLDB_NOT_IMPLEMENTED(); }

std::size_t ColumnChunk::SegmentRowCount() const { CPPCOLDB_NOT_IMPLEMENTED(); }

void ColumnChunk::UpdateCombinedStats(const common::DataVector& /*vec*/, std::size_t /*count*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::storage
