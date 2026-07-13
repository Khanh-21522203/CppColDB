#include "cppcoldb/engine/storage/wal.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::storage {

Wal::Wal(int fd, bool read_only, std::string path)
    : fd_(fd), read_only_(read_only), path_(std::move(path)) {}

std::unique_ptr<Wal> Wal::Create(const std::string& /*path*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

std::unique_ptr<Wal> Wal::OpenForReplay(const std::string& /*path*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteInsert(const std::string& /*schema*/, const std::string& /*table*/,
                       const common::DataChunk& /*chunk*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteDelete(const std::string& /*schema*/, const std::string& /*table*/,
                       const std::vector<common::RowId>& /*row_ids*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteUpdate(const std::string& /*schema*/, const std::string& /*table*/,
                       const std::vector<common::ColumnId>& /*col_ids*/,
                       const std::vector<common::RowId>& /*row_ids*/,
                       const common::DataChunk& /*new_values*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteCreateTable(const std::string& /*schema*/, const std::string& /*table*/,
                            const std::vector<std::string>& /*col_names*/,
                            const std::vector<common::TypeId>& /*col_types*/,
                            const PartitionInfo& /*partition_info*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteDropTable(const std::string& /*schema*/, const std::string& /*table*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteAlterTable(
    const std::string& /*schema*/, const std::string& /*table*/,
    const PartitionInfo& /*new_partition_info*/,
    const std::vector<std::vector<common::RowGroupId>>& /*new_row_group_indices*/) {
    CPPCOLDB_NOT_IMPLEMENTED();
}

void Wal::WriteCreateSchema(const std::string& /*schema*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteDropSchema(const std::string& /*schema*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteCheckpointMarker() { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::Flush() { CPPCOLDB_NOT_IMPLEMENTED(); }

bool Wal::ReadNextEntry(WalEntry& /*out*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::Truncate() { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::Close() { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::AppendEntry(WalEntryType /*type*/, const std::vector<std::uint8_t>& /*payload*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::WriteU8 (std::vector<std::uint8_t>& /*buf*/, std::uint8_t  /*v*/) { CPPCOLDB_NOT_IMPLEMENTED(); }
void Wal::WriteU16(std::vector<std::uint8_t>& /*buf*/, std::uint16_t /*v*/) { CPPCOLDB_NOT_IMPLEMENTED(); }
void Wal::WriteU32(std::vector<std::uint8_t>& /*buf*/, std::uint32_t /*v*/) { CPPCOLDB_NOT_IMPLEMENTED(); }
void Wal::WriteU64(std::vector<std::uint8_t>& /*buf*/, std::uint64_t /*v*/) { CPPCOLDB_NOT_IMPLEMENTED(); }
void Wal::WriteI64(std::vector<std::uint8_t>& /*buf*/, std::int64_t  /*v*/) { CPPCOLDB_NOT_IMPLEMENTED(); }
void Wal::WriteF64(std::vector<std::uint8_t>& /*buf*/, double        /*v*/) { CPPCOLDB_NOT_IMPLEMENTED(); }
void Wal::WriteStr(std::vector<std::uint8_t>& /*buf*/, const std::string& /*s*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

void Wal::SerializeChunk(const common::DataChunk& /*chunk*/, std::vector<std::uint8_t>& /*buf*/) { CPPCOLDB_NOT_IMPLEMENTED(); }

} // namespace cppcoldb::engine::storage
