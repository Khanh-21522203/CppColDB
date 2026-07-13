#include "cppcoldb/engine/transaction/transaction.hpp"

// Transaction is fully defined by trivial inline accessors in the header (see
// CONVENTIONS.md #4); this translation unit exists so the module's stub .cpp
// tree mirrors every header 1:1, and is where non-trivial Transaction methods
// will land as they are added.

namespace cppcoldb::engine::transaction {

} // namespace cppcoldb::engine::transaction
