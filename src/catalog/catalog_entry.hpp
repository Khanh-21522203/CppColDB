#pragma once
#include <string>
#include <vector>
#include <memory>
#include "common/types.hpp"
#include "storage/partition_info.hpp"

namespace cppcoldb {

class RowGroup;
class BufferManager;

struct ColumnDefinition {
    std::string name;
    TypeId      type;
    bool        not_null    = false;
    bool        primary_key = false;
};

// Base class for all catalog entries.
struct CatalogEntry {
    enum class EntryType { TABLE };

    EntryType   entry_type  = EntryType::TABLE;
    std::string name;
    std::string schema_name;

    TransactionId create_tx_id       = INVALID_TRANSACTION;
    timestamp_t   create_commit_time = INVALID_TIMESTAMP;
    TransactionId delete_tx_id       = INVALID_TRANSACTION;
    timestamp_t   delete_commit_time = INVALID_TIMESTAMP;

    virtual ~CatalogEntry() = default;
};

// A table entry in the catalog.
struct TableCatalogEntry : CatalogEntry {
    std::vector<ColumnDefinition>          columns;
    std::vector<std::unique_ptr<RowGroup>> row_groups; // all RowGroups in append order
    BufferManager*                         bm_ = nullptr;

    // Partition metadata (NONE for non-partitioned tables).
    PartitionInfo partition_info;
    // partition_rg_indices[pid] = sorted list of indices into row_groups for that partition.
    // Only populated when partition_info.IsPartitioned().
    std::vector<std::vector<size_t>> partition_rg_indices;

    size_t ColumnCount() const { return columns.size(); }
    int    FindColumn(const std::string& col_name) const;
    std::vector<std::string> ColumnNames() const;
    std::vector<TypeId>      ColumnTypes() const;

    // Return the last RowGroup if it has space, otherwise create a new one.
    // For non-partitioned tables only.
    RowGroup* GetOrAddRowGroup();

    // Return the last RowGroup for partition pid if it has space, otherwise create one.
    // The new RowGroup is added to both row_groups and partition_rg_indices[pid].
    RowGroup* GetOrAddRowGroupForPartition(uint32_t pid);
};

} // namespace cppcoldb
