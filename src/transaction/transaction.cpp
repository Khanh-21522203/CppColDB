#include "transaction/transaction.hpp"
#include "transaction/undo_buffer.hpp"
#include "storage/wal.hpp"

namespace cppcoldb {

void Transaction::WriteToWAL(WAL& wal) const {
    undo_buffer.ForEachForward([&](const UndoEntry& entry) {
        if (const auto* e = std::get_if<CatalogUndoEntry>(&entry)) {
            if (e->was_create) {
                wal.WriteCreateTable(e->schema, e->table, e->col_names, e->col_types);
            } else {
                wal.WriteDropTable(e->schema, e->table);
            }

        } else if (const auto* e = std::get_if<InsertUndoEntry>(&entry)) {
            if (e->inserted_rows.count > 0) {
                wal.WriteInsert(e->schema, e->table, e->inserted_rows);
            }

        } else if (const auto* e = std::get_if<DeleteUndoEntry>(&entry)) {
            if (!e->row_ids.empty()) {
                wal.WriteDelete(e->schema, e->table, e->row_ids);
            }

        } else if (const auto* e = std::get_if<UpdateUndoEntry>(&entry)) {
            if (!e->row_ids.empty() && e->new_values.count > 0) {
                wal.WriteUpdate(e->schema, e->table, e->col_ids, e->row_ids, e->new_values);
            }
        }
    });
    wal.Flush();
}

} // namespace cppcoldb
