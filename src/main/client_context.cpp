#include "main/client_context.hpp"
#include "parser/tokenizer.hpp"
#include "parser/parser.hpp"
#include "parser/ast/parsed_statement.hpp"
#include "planner/binder.hpp"
#include "planner/optimizer.hpp"
#include "planner/physical_planner.hpp"
#include "planner/logical_plan/logical_plan.hpp"
#include "execution/executor.hpp"
#include "catalog/catalog.hpp"
#include "transaction/transaction.hpp"
#include "common/exception.hpp"
#include "storage/partition_info.hpp"
#include <sstream>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// Plain EXPLAIN formatter — formats a logical plan tree as indented text.
// ---------------------------------------------------------------------------
static std::string FormatLogicalPlan(const LogicalPlan& node, int depth = 0) {
    std::string indent(static_cast<size_t>(depth * 2), ' ');
    std::ostringstream oss;

    switch (node.node_type) {
        case LogicalPlan::Type::GET: {
            const auto& g = static_cast<const LogicalGet&>(node);
            oss << indent << "Scan " << g.schema_name << "." << g.table_name;
            if (g.partition_info.IsPartitioned()) {
                const auto& pi = g.partition_info;
                const char* pt = (pi.type == PartitionType::RANGE) ? "RANGE"
                               : (pi.type == PartitionType::HASH)  ? "HASH"
                                                                    : "LIST";
                oss << " [partition=" << pt << "(";
                for (size_t k = 0; k < pi.partition_cols.size(); ++k) {
                    if (k) oss << ",";
                    oss << pi.partition_cols[k];
                }
                oss << "), partitions=" << pi.num_partitions << "]";
            }
            break;
        }
        case LogicalPlan::Type::FILTER:
            oss << indent << "Filter";
            break;
        case LogicalPlan::Type::PROJECTION:
            oss << indent << "Projection";
            break;
        case LogicalPlan::Type::LIMIT: {
            const auto& l = static_cast<const LogicalLimit&>(node);
            oss << indent << "Limit";
            if (l.limit >= 0)  oss << " limit=" << l.limit;
            if (l.offset > 0)  oss << " offset=" << l.offset;
            break;
        }
        case LogicalPlan::Type::SORT:
            oss << indent << "Sort";
            break;
        case LogicalPlan::Type::AGGREGATE:
            oss << indent << "Aggregate";
            break;
        case LogicalPlan::Type::JOIN:
            oss << indent << "HashJoin";
            break;
        case LogicalPlan::Type::INSERT: {
            const auto& ins = static_cast<const LogicalInsert&>(node);
            oss << indent << "Insert " << ins.schema_name << "." << ins.table_name;
            break;
        }
        case LogicalPlan::Type::DELETE: {
            const auto& del = static_cast<const LogicalDelete&>(node);
            oss << indent << "Delete " << del.schema_name << "." << del.table_name;
            break;
        }
        case LogicalPlan::Type::UPDATE: {
            const auto& upd = static_cast<const LogicalUpdate&>(node);
            oss << indent << "Update " << upd.schema_name << "." << upd.table_name;
            break;
        }
        case LogicalPlan::Type::CREATE_TABLE: {
            const auto& ct = static_cast<const LogicalCreateTable&>(node);
            oss << indent << "CreateTable " << ct.schema_name << "." << ct.table_name;
            break;
        }
        case LogicalPlan::Type::DROP_TABLE: {
            const auto& dt = static_cast<const LogicalDropTable&>(node);
            oss << indent << "DropTable " << dt.schema_name << "." << dt.table_name;
            break;
        }
        case LogicalPlan::Type::ALTER_TABLE: {
            const auto& at = static_cast<const LogicalAlterTable&>(node);
            if (at.kind == LogicalAlterTable::AlterKind::DROP_PARTITION)
                oss << indent << "AlterTable " << at.schema_name << "." << at.table_name
                    << " DROP PARTITION " << at.partition_idx;
            else
                oss << indent << "AlterTable " << at.schema_name << "." << at.table_name
                    << " ADD PARTITION";
            break;
        }
        default:
            oss << indent << "Unknown";
            break;
    }
    for (const auto& child : node.children) {
        oss << "\n" << FormatLogicalPlan(*child, depth + 1);
    }
    return oss.str();
}

QueryResult ClientContext::Query(const std::string& sql) {
    bool explain_analyze = false;

    // RAII to restore profiling_enabled_ on both normal and exception paths.
    struct ProfRestore {
        bool& flag;
        bool  restore_to;
        ~ProfRestore() { flag = restore_to; }
    } prof_restore{profiling_enabled_, profiling_enabled_};

    try {
        // --- PARSE ---
        if (profiling_enabled_) {
            profiler_.StartQuery(sql);
            profiler_.StartPhase(QueryPhase::PARSE);
        }

        Tokenizer tok(sql);
        auto tokens = tok.Tokenize();
        Parser parser(std::move(tokens));
        auto stmt = parser.Parse();

        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::PARSE);

        // --- Plain EXPLAIN intercept (no ANALYZE) ---
        if (auto* explain = dynamic_cast<ExplainStatement*>(stmt.get())) {
            if (!explain->analyze && explain->inner) {
                if (profiling_enabled_) profiler_.EndQuery(); // discard partial profile
                Binder binder_ex(*catalog, *transaction);
                auto logical_ex = binder_ex.Bind(*explain->inner);
                std::string plan_text = FormatLogicalPlan(*logical_ex);
                QueryResult r;
                r.success      = true;
                r.column_names = {"QUERY PLAN"};
                r.column_types = {TypeId::VARCHAR};
                DataChunk chunk;
                chunk.Initialize({TypeId::VARCHAR});
                chunk.count = 1;
                chunk.columns[0].str_data.resize(1);
                chunk.columns[0].str_data[0] = std::move(plan_text);
                chunk.columns[0].validity.set(0);
                chunk.columns[0].count = 1;
                r.chunks.push_back(std::move(chunk));
                return r;
            }
        }

        // --- EXPLAIN ANALYZE intercept ---
        ParsedStatement* inner_stmt = stmt.get();
        if (auto* explain = dynamic_cast<ExplainStatement*>(stmt.get())) {
            if (explain->analyze && explain->inner) {
                explain_analyze       = true;
                inner_stmt            = explain->inner.get();
                prof_restore.restore_to = false; // will restore to false at end

                if (!profiling_enabled_) {
                    // Force profiling on; retroactively emit a 0-duration PARSE phase.
                    profiling_enabled_ = true;
                    profiler_.StartQuery(sql);
                    profiler_.StartPhase(QueryPhase::PARSE);
                    profiler_.EndPhase(QueryPhase::PARSE);
                }
            }
        }

        // --- BIND ---
        if (profiling_enabled_) profiler_.StartPhase(QueryPhase::BIND);
        Binder binder(*catalog, *transaction);
        auto logical = binder.Bind(*inner_stmt);
        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::BIND);

        // --- OPTIMIZE ---
        if (profiling_enabled_) profiler_.StartPhase(QueryPhase::OPTIMIZE);
        Optimizer opt;
        logical = opt.Optimize(std::move(logical));
        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::OPTIMIZE);

        // --- PHYSICAL PLAN ---
        if (profiling_enabled_) profiler_.StartPhase(QueryPhase::PHYSICAL_PLAN);
        PhysicalPlanner pp;
        auto physical = pp.Plan(*logical);
        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::PHYSICAL_PLAN);

        // --- EXECUTE ---
        Executor exec(*this);
        exec.Initialize(std::move(physical));

        if (profiling_enabled_) profiler_.StartPhase(QueryPhase::EXECUTE);
        exec.Execute();
        if (profiling_enabled_) profiler_.EndPhase(QueryPhase::EXECUTE);

        QueryResult result = exec.GetResult();

        // --- Finalize profiling ---
        if (profiling_enabled_) {
            auto prof_result = profiler_.EndQuery();

            if (explain_analyze) {
                // Return profiling text as single-column VARCHAR result.
                std::string plan_text = prof_result.ToString();
                QueryResult explain_out;
                explain_out.success       = true;
                explain_out.column_names  = {"QUERY PLAN"};
                explain_out.column_types  = {TypeId::VARCHAR};
                DataChunk chunk;
                chunk.Initialize({TypeId::VARCHAR});
                chunk.count = 1;
                chunk.columns[0].str_data.resize(1);
                chunk.columns[0].str_data[0] = std::move(plan_text);
                chunk.columns[0].validity.set(0);
                chunk.columns[0].count = 1;
                explain_out.chunks.push_back(std::move(chunk));
                explain_out.profiling_result = std::move(prof_result);
                return explain_out;
            }

            result.profiling_result = std::move(prof_result);
        }

        return result;

    } catch (const CppColDBException& e) {
        if (profiling_enabled_) profiler_.EndQuery(); // discard partial profile
        QueryResult err;
        err.success       = false;
        err.error_message = e.what();
        return err;
    } catch (const std::exception& e) {
        if (profiling_enabled_) profiler_.EndQuery();
        QueryResult err;
        err.success       = false;
        err.error_message = e.what();
        return err;
    }
}

} // namespace cppcoldb
