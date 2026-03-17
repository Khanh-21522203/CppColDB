#include "main/connection.hpp"
#include "main/database.hpp"
#include "transaction/transaction_manager.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"
#include <cctype>
#include <cstring>

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
    // Intercept SQL transaction control statements before auto-commit logic.
    auto starts_with_kw = [&](const char* kw) -> bool {
        size_t i = 0;
        while (i < sql.size() && std::isspace((unsigned char)sql[i])) ++i;
        size_t klen = std::strlen(kw);
        if (sql.size() - i < klen) return false;
        for (size_t j = 0; j < klen; ++j)
            if (std::tolower((unsigned char)sql[i + j]) != kw[j]) return false;
        size_t end = i + klen;
        while (end < sql.size() && std::isspace((unsigned char)sql[end])) ++end;
        if (end < sql.size() && sql[end] == ';') {
            ++end;
            while (end < sql.size() && std::isspace((unsigned char)sql[end])) ++end;
        }
        return end == sql.size();
    };

    auto make_ok  = []() { QueryResult r; r.success = true;  return r; };
    auto make_err = [](const std::string& msg) {
        QueryResult r; r.success = false; r.error_message = msg; return r;
    };

    if (starts_with_kw("begin")) {
        try { Begin(); return make_ok(); }
        catch (const std::exception& e) { return make_err(e.what()); }
    }
    if (starts_with_kw("commit")) {
        try { Commit(); return make_ok(); }
        catch (const std::exception& e) { return make_err(e.what()); }
    }
    if (starts_with_kw("rollback")) {
        try { Rollback(); return make_ok(); }
        catch (const std::exception& e) { return make_err(e.what()); }
    }

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
