#pragma once
#include <string>
#include "execution/executor.hpp"

namespace cppcoldb {

class Catalog;
class Transaction;
class TransactionManager;
class WAL;

// Per-connection context. Operators read catalog/transaction from this struct.
// Query() drives the full parse→bind→optimize→plan→execute pipeline.
struct ClientContext {
    Catalog*            catalog     = nullptr;
    Transaction*        transaction = nullptr;
    TransactionManager* txn_manager = nullptr;
    WAL*                wal         = nullptr;   // nullptr for in-memory

    // Execute one SQL statement. The caller is responsible for having set
    // catalog and transaction before calling this (Connection does so).
    QueryResult Query(const std::string& sql);
};

} // namespace cppcoldb
