#include "cppcoldb/engine/catalog/catalog_entry.hpp"

#include "cppcoldb/common/not_implemented.hpp"

namespace cppcoldb::engine::catalog {

int TableCatalogEntry::FindColumn(const std::string& /*col_name*/) const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::vector<std::string> TableCatalogEntry::ColumnNames() const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

std::vector<common::TypeId> TableCatalogEntry::ColumnTypes() const {
    CPPCOLDB_NOT_IMPLEMENTED();
}

} // namespace cppcoldb::engine::catalog
