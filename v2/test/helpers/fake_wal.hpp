#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "cppcoldb/common/not_implemented.hpp"
#include "cppcoldb/engine/abstractions/storage/i_wal.hpp"

namespace cppcoldb::test {

// Compile-time stand-in for IWal. Not executed; every method throws.
class FakeWal final : public engine::storage::IWal {
public:
    void WriteInsert(const std::string& schema, const std::string& table,
                      const common::DataChunk& chunk) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void WriteDelete(const std::string& schema, const std::string& table,
                      const std::vector<common::RowId>& row_ids) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void WriteUpdate(const std::string& schema, const std::string& table,
                      const std::vector<common::ColumnId>& col_ids,
                      const std::vector<common::RowId>& row_ids,
                      const common::DataChunk& new_values) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void WriteCreateTable(const std::string& schema, const std::string& table,
                           const std::vector<std::string>& col_names,
                           const std::vector<common::TypeId>& col_types,
                           const engine::storage::PartitionInfo& partition_info) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }
    void WriteDropTable(const std::string& schema, const std::string& table) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void WriteAlterTable(
        const std::string& schema, const std::string& table,
        const engine::storage::PartitionInfo& new_partition_info,
        const std::vector<std::vector<common::RowGroupId>>& new_row_group_indices) override {
        CPPCOLDB_NOT_IMPLEMENTED();
    }

    void WriteCreateSchema(const std::string& schema) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void WriteDropSchema(const std::string& schema) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void WriteCheckpointMarker() override { CPPCOLDB_NOT_IMPLEMENTED(); }

    void Flush() override { CPPCOLDB_NOT_IMPLEMENTED(); }
    bool ReadNextEntry(engine::storage::WalEntry& out) override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void Truncate() override { CPPCOLDB_NOT_IMPLEMENTED(); }
    void Close() override { CPPCOLDB_NOT_IMPLEMENTED(); }

    bool        IsEmpty()       const override { CPPCOLDB_NOT_IMPLEMENTED(); }
    std::size_t FileSizeBytes() const override { CPPCOLDB_NOT_IMPLEMENTED(); }
};

} // namespace cppcoldb::test
