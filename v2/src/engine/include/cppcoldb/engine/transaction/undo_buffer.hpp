#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "cppcoldb/common/types/column_id.hpp"
#include "cppcoldb/common/types/data_chunk.hpp"
#include "cppcoldb/common/types/row_group_id.hpp"
#include "cppcoldb/common/types/row_id.hpp"
#include "cppcoldb/common/types/type_id.hpp"

namespace cppcoldb::engine::transaction {

// Undo record for a catalog DDL change (CREATE/DROP schema or table).
struct CatalogUndoEntry {
    std::string schema;
    std::string table;
    bool        was_create = true; // true = undo a CREATE; false = undo a DROP
    // Filled for CREATE TABLE so WAL logging at commit can serialize the schema.
    std::vector<std::string>    col_names;
    std::vector<common::TypeId> col_types;
};

// Undo record for a row insert: rollback drops the appended rows.
struct InsertUndoEntry {
    std::string         schema;
    std::string         table;
    common::RowGroupId  row_group_id;
    std::size_t         append_start = 0;
    std::size_t         append_count = 0;
    common::DataChunk   inserted_rows; // materialized for commit-time WAL logging
};

// Undo record for a row delete: rollback un-deletes the given rows.
struct DeleteUndoEntry {
    std::string                schema;
    std::string                table;
    std::vector<common::RowId> row_ids;
};

// Undo record for a row update: rollback restores old_values.
struct UpdateUndoEntry {
    std::string                   schema;
    std::string                   table;
    std::vector<common::RowId>    row_ids;
    std::vector<common::ColumnId> col_ids;
    common::DataChunk             old_values; // for rollback
    common::DataChunk             new_values; // for WAL redo
};

using UndoEntry = std::variant<CatalogUndoEntry, InsertUndoEntry, DeleteUndoEntry, UpdateUndoEntry>;

// Per-transaction log of reversible changes: replayed in reverse on rollback,
// drained forward at commit time to produce WAL records.
class UndoBuffer final {
public:
    UndoBuffer() = default;

    void PushCatalogEntry(CatalogUndoEntry e) { entries_.push_back(std::move(e)); }
    void PushInsert(InsertUndoEntry e) { entries_.push_back(std::move(e)); }
    void PushDelete(DeleteUndoEntry e) { entries_.push_back(std::move(e)); }
    void PushUpdate(UpdateUndoEntry e) { entries_.push_back(std::move(e)); }

    void ForEachForward(const std::function<void(const UndoEntry&)>& fn) const;
    void ForEachReverse(const std::function<void(const UndoEntry&)>& fn) const;

    bool IsEmpty() const { return entries_.empty(); }
    void Clear() { entries_.clear(); }

private:
    std::vector<UndoEntry> entries_;
};

} // namespace cppcoldb::engine::transaction
