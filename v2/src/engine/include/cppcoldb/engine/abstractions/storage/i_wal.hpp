#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/row_group_id.hpp"
#include "cppcoldb/common/types/row_id.hpp"
#include "cppcoldb/common/types/type_id.hpp"

namespace cppcoldb::engine::storage {

// PartitionInfo is a plain value type owned by this same domain
// (see cppcoldb/engine/storage/partition_info.hpp); forward-declared here so
// the interface never includes a concrete implementation header.
struct PartitionInfo;

enum class WalEntryType : std::uint8_t {
    CREATE_TABLE  = 1,
    DROP_TABLE    = 2,
    INSERT        = 3,
    DELETE        = 4,
    UPDATE        = 5,
    CREATE_SCHEMA = 6,
    DROP_SCHEMA   = 7,
    CHECKPOINT    = 8,
    ALTER_TABLE   = 9,
};

// One decoded WAL entry, produced during replay.
struct WalEntry {
    WalEntryType               type = WalEntryType::INSERT;
    std::vector<std::uint8_t> data; // raw payload
};

// Durable, sequential, append-only record of mutating operations. Entries are
// buffered in memory until Flush() and replayed via ReadNextEntry() on recovery.
class IWal {
public:
    virtual ~IWal() = default;

    virtual void WriteInsert(const std::string& schema, const std::string& table,
                              const common::DataChunk& chunk) = 0;
    virtual void WriteDelete(const std::string& schema, const std::string& table,
                              const std::vector<common::RowId>& row_ids) = 0;
    virtual void WriteUpdate(const std::string& schema, const std::string& table,
                              const std::vector<common::ColumnId>& col_ids,
                              const std::vector<common::RowId>& row_ids,
                              const common::DataChunk& new_values) = 0;
    virtual void WriteCreateTable(const std::string& schema, const std::string& table,
                                   const std::vector<std::string>& col_names,
                                   const std::vector<common::TypeId>& col_types,
                                   const PartitionInfo& partition_info) = 0;
    virtual void WriteDropTable(const std::string& schema, const std::string& table) = 0;

    // ALTER TABLE: write the new partition state (result of DROP/ADD PARTITION).
    virtual void WriteAlterTable(
        const std::string& schema, const std::string& table,
        const PartitionInfo& new_partition_info,
        const std::vector<std::vector<common::RowGroupId>>& new_row_group_indices) = 0;

    virtual void WriteCreateSchema(const std::string& schema) = 0;
    virtual void WriteDropSchema(const std::string& schema) = 0;
    virtual void WriteCheckpointMarker() = 0;

    // Flush the write buffer to durable storage. Throws IOError on failure.
    virtual void Flush() = 0;

    // Replay: read the next entry from the log. Returns false at EOF / truncated entry.
    virtual bool ReadNextEntry(WalEntry& out) = 0;

    // Remove all entries before the last checkpoint marker.
    virtual void Truncate() = 0;

    virtual void Close() = 0;

    virtual bool        IsEmpty()       const = 0;
    virtual std::size_t FileSizeBytes() const = 0;
};

} // namespace cppcoldb::engine::storage
