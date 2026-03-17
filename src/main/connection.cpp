#include "main/connection.hpp"
#include "main/database.hpp"
#include "transaction/transaction_manager.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"

namespace cppcoldb {

Connection::Connection(Database& db) : db_(db) {
    ctx_.catalog     = &db.GetCatalog();
    ctx_.txn_manager = &db.GetTransactionManager();
    ctx_.wal         = db.IsInMemory() ? nullptr : &db.GetWAL();
    db.RegisterConnection(this);
}

Connection::~Connection() {
    if (active_tx_) {
        try { db_.GetTransactionManager().Rollback(active_tx_); } catch (...) {}
        active_tx_.reset();
    }
    db_.UnregisterConnection(this);
}

QueryResult Connection::Query(const std::string& sql) {
    bool auto_commit_this = (active_tx_ == nullptr);
    if (auto_commit_this) {
        active_tx_ = db_.GetTransactionManager().BeginTransaction(true);
    }

    ctx_.transaction = active_tx_.get();

    QueryResult result = ctx_.Query(sql);

    if (auto_commit_this) {
        if (result.success) {
            db_.GetTransactionManager().Commit(active_tx_);
        } else {
            db_.GetTransactionManager().Rollback(active_tx_);
        }
        active_tx_.reset();
        ctx_.transaction = nullptr;
    }

    return result;
}

void Connection::Begin() {
    if (active_tx_) throw RuntimeError("Transaction already active");
    active_tx_ = db_.GetTransactionManager().BeginTransaction(false);
    ctx_.transaction = active_tx_.get();
}

void Connection::Commit() {
    if (!active_tx_) throw RuntimeError("No active transaction");
    db_.GetTransactionManager().Commit(active_tx_);
    active_tx_.reset();
    ctx_.transaction = nullptr;
}

void Connection::Rollback() {
    if (!active_tx_) throw RuntimeError("No active transaction");
    db_.GetTransactionManager().Rollback(active_tx_);
    active_tx_.reset();
    ctx_.transaction = nullptr;
}

} // namespace cppcoldb
