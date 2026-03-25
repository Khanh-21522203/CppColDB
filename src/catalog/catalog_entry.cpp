#include "catalog/catalog_entry.hpp"
#include "storage/column/row_group.hpp"

namespace cppcoldb {

int TableCatalogEntry::FindColumn(const std::string& col_name) const {
    for (int i = 0; i < static_cast<int>(columns.size()); ++i) {
        if (columns[i].name == col_name) return i;
    }
    return -1;
}

std::vector<std::string> TableCatalogEntry::ColumnNames() const {
    std::vector<std::string> names;
    names.reserve(columns.size());
    for (const auto& c : columns) names.push_back(c.name);
    return names;
}

std::vector<TypeId> TableCatalogEntry::ColumnTypes() const {
    std::vector<TypeId> types;
    types.reserve(columns.size());
    for (const auto& c : columns) types.push_back(c.type);
    return types;
}

RowGroup* TableCatalogEntry::GetOrAddRowGroup() {
    if (!row_groups.empty() &&
        row_groups.back()->RowCount() < ROW_GROUP_SIZE) {
        return row_groups.back().get();
    }
    uint32_t rg_id = static_cast<uint32_t>(row_groups.size());
    auto rg = std::make_unique<RowGroup>(rg_id, ColumnTypes(), *bm_);
    RowGroup* ptr = rg.get();
    row_groups.push_back(std::move(rg));
    return ptr;
}

RowGroup* TableCatalogEntry::GetOrAddRowGroupForPartition(uint32_t pid) {
    auto& indices = partition_rg_indices[pid];
    if (!indices.empty()) {
        RowGroup* last = row_groups[indices.back()].get();
        if (last->RowCount() < ROW_GROUP_SIZE)
            return last;
    }
    uint32_t new_rg_id = static_cast<uint32_t>(row_groups.size());
    auto rg = std::make_unique<RowGroup>(new_rg_id, ColumnTypes(), *bm_);
    RowGroup* ptr = rg.get();
    row_groups.push_back(std::move(rg));
    indices.push_back(new_rg_id);
    return ptr;
}

} // namespace cppcoldb
