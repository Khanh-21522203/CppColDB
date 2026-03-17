#pragma once
#include <memory>
#include <string>
#include "main/client_context.hpp"

namespace cppcoldb {

class Database;
class Transaction;

class Connection {
public:
    explicit Connection(Database& db);
    ~Connection();

    // Execute a SQL statement (auto-commit if no explicit transaction is active).
    QueryResult Query(const std::string& sql);

    // Explicit transaction control.
    void Begin();
    void Commit();
    void Rollback();

    Database& GetDatabase() { return db_; }

private:
    Database&                    db_;
    ClientContext                ctx_;
    std::shared_ptr<Transaction> active_tx_; // null when auto-commit
};

} // namespace cppcoldb
