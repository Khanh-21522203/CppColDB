#include "transaction/transaction.hpp"

namespace cppcoldb {

// WriteToWAL: full implementation deferred to Phase 9.
// For now, catalog-level WAL entries are written; row-level entries are
// handled by the execution layer (Phase 9).
void Transaction::WriteToWAL(WAL& /*wal*/) const {
    // Stub: called only when WAL* is non-null (not used in Phase 4 tests).
}

} // namespace cppcoldb
