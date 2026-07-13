#pragma once
#include <memory>
#include <string>

#include "cppcoldb/query_result.hpp"
#include "cppcoldb/engine/abstractions/catalog/i_catalog.hpp"
#include "cppcoldb/engine/abstractions/storage/i_wal.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction.hpp"
#include "cppcoldb/engine/abstractions/transaction/i_transaction_manager.hpp"
#include "cppcoldb/engine/execution/executor.hpp"
#include "cppcoldb/engine/profiler/query_profiler.hpp"
#include "cppcoldb/sql/parser/parser.hpp"
#include "cppcoldb/sql/planner/binder.hpp"
#include "cppcoldb/sql/planner/optimizer.hpp"
#include "cppcoldb/sql/planner/physical_planner.hpp"

namespace cppcoldb {

// Per-connection, per-query orchestrator. Operators and the SQL front end
// read the catalog/transaction/wal pointers from this struct-like class; the
// owning Connection is responsible for setting them before calling Query().
//
// Query() drives the full pipeline: parser::Parser -> planner::Binder ->
// planner::Optimizer -> planner::PhysicalPlanner -> execution::Executor,
// producing one QueryResult.
//
// NOTE: engine::execution (see physical_result_collector.hpp) forward-declares
// `cppcoldb::ClientContext` in the root namespace; this definition matches it.
class ClientContext {
public:
    ClientContext();
    ~ClientContext();

    // End-to-end entry point for one SQL statement.
    QueryResult Query(const std::string& sql);

    engine::catalog::ICatalog*                catalog     = nullptr;
    engine::transaction::ITransaction*         transaction = nullptr;
    engine::transaction::ITransactionManager*  txn_manager = nullptr;
    engine::storage::IWal*                     wal         = nullptr; // nullptr for in-memory

private:
    std::unique_ptr<sql::parser::Parser>           parser_;
    std::unique_ptr<sql::planner::Binder>          binder_;
    std::unique_ptr<sql::planner::Optimizer>       optimizer_;
    std::unique_ptr<sql::planner::PhysicalPlanner> physical_planner_;
    std::unique_ptr<engine::execution::Executor>   executor_;
    engine::profiler::QueryProfiler                profiler_;
};

} // namespace cppcoldb
