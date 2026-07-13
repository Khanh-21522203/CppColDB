// Placeholder stub for cppcoldb::engine::catalog::Catalog.
// Also exercises compilation of FakeCatalog and FakeSchemaStore (the fakes
// for ICatalog and ISchemaStore). No assertions yet.
#include "cppcoldb/engine/catalog/catalog.hpp"

#include "helpers/fake_catalog.hpp"
#include "helpers/fake_schema_store.hpp"

namespace cppcoldb::test {

void PlaceholderTestCatalog() {
    // TODO: add assertions once Catalog DDL/MVCC-aware lookups are implemented.
}

} // namespace cppcoldb::test
