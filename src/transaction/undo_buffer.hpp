#pragma once
#include <vector>
#include <variant>
#include <functional>
#include "common/types.hpp"
#include "storage/partition_info.hpp"

namespace cppcoldb {

struct CatalogUndoEntry {
    std::string schema;
    std::string table;
    bool        was_create; // true = undo a CREATE; false = undo a DROP
    // Filled for CREATE TABLE so WAL logging at commit can serialize schema.
    std::vector<std::string> col_names;
    std::vector<TypeId>      col_types;
    PartitionInfo            partition_info;
};

struct InsertUndoEntry {
    std::string schema;
    std::string table;
    size_t      row_group_id;
    size_t      append_start;
    size_t      append_count;
    // Materialized inserted rows for commit-time WAL logging.
    DataChunk   inserted_rows;
};

struct DeleteUndoEntry {
    std::string         schema;
    std::string         table;
    std::vector<row_t>  row_ids;
};

struct UpdateUndoEntry {
    std::string         schema;
    std::string         table;
    std::vector<row_t>  row_ids;
    std::vector<size_t> col_ids;
    DataChunk           old_values; // for rollback
    DataChunk           new_values; // for WAL (redo)
};

using UndoEntry = std::variant<CatalogUndoEntry, InsertUndoEntry,
                                DeleteUndoEntry,  UpdateUndoEntry>;

class UndoBuffer {
public:
    void PushCatalogEntry(CatalogUndoEntry e) { entries_.push_back(std::move(e)); }
    void PushInsert(InsertUndoEntry e)         { entries_.push_back(std::move(e)); }
    void PushDelete(DeleteUndoEntry e)         { entries_.push_back(std::move(e)); }
    void PushUpdate(UpdateUndoEntry e)         { entries_.push_back(std::move(e)); }

    void ForEachForward(std::function<void(const UndoEntry&)> fn) const;
    void ForEachReverse(std::function<void(const UndoEntry&)> fn) const;

    bool IsEmpty() const { return entries_.empty(); }
    void Clear()         { entries_.clear(); }

private:
    std::vector<UndoEntry> entries_;
};

} // namespace cppcoldb
