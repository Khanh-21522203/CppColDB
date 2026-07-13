// Placeholder stub for cppcoldb::engine::transaction::Transaction.
// Also exercises compilation of FakeTransaction and FakeTransactionManager
// (the fakes for ITransaction and ITransactionManager). No assertions yet.
#include "cppcoldb/engine/transaction/transaction.hpp"

#include "helpers/fake_transaction.hpp"
#include "helpers/fake_transaction_manager.hpp"

namespace cppcoldb::test {

void PlaceholderTestTransaction() {
    // TODO: add assertions once Transaction lifecycle (begin/commit/rollback) is implemented.
}

} // namespace cppcoldb::test
