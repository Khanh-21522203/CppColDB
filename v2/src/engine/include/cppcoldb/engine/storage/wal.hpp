#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/row_group_id.hpp"
#include "cppcoldb/common/types/row_id.hpp"
#include "cppcoldb/common/types/type_id.hpp"
#include "cppcoldb/engine/abstractions/storage/i_wal.hpp"
#include "cppcoldb/engine/storage/partition_info.hpp"

namespace cppcoldb::engine::storage {

// Append-only, sequential write-ahead log with checkpoint-based truncation.
class Wal final : public IWal {
public:
    // Create a new WAL file (truncates if it exists).
    static std::unique_ptr<Wal> Create(const std::string& path);

    // Open an existing WAL file for sequential replay.
    static std::unique_ptr<Wal> OpenForReplay(const std::string& path);

    ~Wal() override = default;

    void WriteInsert(const std::string& schema, const std::string& table,
                      const common::DataChunk& chunk) override;
    void WriteDelete(const std::string& schema, const std::string& table,
                      const std::vector<common::RowId>& row_ids) override;
    void WriteUpdate(const std::string& schema, const std::string& table,
                      const std::vector<common::ColumnId>& col_ids,
                      const std::vector<common::RowId>& row_ids,
                      const common::DataChunk& new_values) override;
    void WriteCreateTable(const std::string& schema, const std::string& table,
                           const std::vector<std::string>& col_names,
                           const std::vector<common::TypeId>& col_types,
                           const PartitionInfo& partition_info) override;
    void WriteDropTable(const std::string& schema, const std::string& table) override;
    void WriteAlterTable(
        const std::string& schema, const std::string& table,
        const PartitionInfo& new_partition_info,
        const std::vector<std::vector<common::RowGroupId>>& new_row_group_indices) override;
    void WriteCreateSchema(const std::string& schema) override;
    void WriteDropSchema(const std::string& schema) override;
    void WriteCheckpointMarker() override;

    void Flush() override;
    bool ReadNextEntry(WalEntry& out) override;
    void Truncate() override;
    void Close() override;

    bool        IsEmpty()       const override { return file_size_ == 0; }
    std::size_t FileSizeBytes() const override { return file_size_; }

private:
    Wal(int fd, bool read_only, std::string path);

    void AppendEntry(WalEntryType type, const std::vector<std::uint8_t>& payload);

    // Serialization helpers (append to buf).
    static void WriteU8 (std::vector<std::uint8_t>& buf, std::uint8_t  v);
    static void WriteU16(std::vector<std::uint8_t>& buf, std::uint16_t v);
    static void WriteU32(std::vector<std::uint8_t>& buf, std::uint32_t v);
    static void WriteU64(std::vector<std::uint8_t>& buf, std::uint64_t v);
    static void WriteI64(std::vector<std::uint8_t>& buf, std::int64_t  v);
    static void WriteF64(std::vector<std::uint8_t>& buf, double        v);
    static void WriteStr(std::vector<std::uint8_t>& buf, const std::string& s);

    static void SerializeChunk(const common::DataChunk& chunk, std::vector<std::uint8_t>& buf);

    int                       fd_        = -1;
    bool                      read_only_ = false;
    std::string               path_;
    std::vector<std::uint8_t> write_buffer_;
    std::size_t               file_size_      = 0;
    std::size_t               checkpoint_pos_ = 0;
};

} // namespace cppcoldb::engine::storage
