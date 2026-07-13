#pragma once
#include <memory>
#include <string>

#include "cppcoldb/client_context.hpp"
#include "cppcoldb/query_result.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"

namespace cppcoldb {

class Database;

// One client session against a Database. Owns a ClientContext and, when an
// explicit transaction is active, the ITransaction handle it is bound to.
class Connection {
public:
    explicit Connection(Database& db);
    ~Connection();

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    // Execute a SQL statement (auto-commit if no explicit transaction is active).
    QueryResult Query(const std::string& sql);

    // Explicit transaction control.
    void Begin();
    void Commit();
    void Rollback();

    Database& GetDatabase() { return db_; }

private:
    Database&                                          db_;
    ClientContext                                      ctx_;
    std::shared_ptr<engine::transaction::ITransaction> active_transaction_; // null when auto-commit
};

} // namespace cppcoldb
