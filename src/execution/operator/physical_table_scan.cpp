#include "execution/operator/physical_table_scan.hpp"
#include "execution/operator/expr_evaluator.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction.hpp"
#include "storage/column/row_group.hpp"
#include "common/exception.hpp"

#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace cppcoldb {

namespace {

// Try to extract a simple "BoundColumnRef op Literal" predicate from an expression.
// Returns true and fills pred on success. col_ref op lit and lit op col_ref are both handled.
bool TryExtractPredicate(const LogicalExpr& expr, ScanPredicate& pred) {
    if (expr.kind != LogicalExpr::Kind::BINARY_OP) return false;
    const auto& bop = static_cast<const LogicalBinaryOp&>(expr);
    if (!bop.left || !bop.right) return false;

    const BoundColumnRef* col = nullptr;
    const LogicalLit*     lit = nullptr;
    bool                  flipped = false; // lit is on the left

    if (bop.left->kind  == LogicalExpr::Kind::BOUND_COLUMN_REF &&
        bop.right->kind == LogicalExpr::Kind::LITERAL) {
        col = static_cast<const BoundColumnRef*>(bop.left.get());
        lit = static_cast<const LogicalLit*>(bop.right.get());
    } else if (bop.left->kind  == LogicalExpr::Kind::LITERAL &&
               bop.right->kind == LogicalExpr::Kind::BOUND_COLUMN_REF) {
        lit = static_cast<const LogicalLit*>(bop.left.get());
        col = static_cast<const BoundColumnRef*>(bop.right.get());
        flipped = true;
    } else {
        return false;
    }

    if (lit->is_null) return false;

    // Map op string to ScanPredicateOp; flip direction when literal is on the left.
    ScanPredicateOp op;
    const std::string& s = bop.op;
    if      (s == "=")  op = ScanPredicateOp::EQ;
    else if (s == "<")  op = flipped ? ScanPredicateOp::GT : ScanPredicateOp::LT;
    else if (s == "<=") op = flipped ? ScanPredicateOp::GE : ScanPredicateOp::LE;
    else if (s == ">")  op = flipped ? ScanPredicateOp::LT : ScanPredicateOp::GT;
    else if (s == ">=") op = flipped ? ScanPredicateOp::LE : ScanPredicateOp::GE;
    else return false; // AND/OR/etc. — not a simple comparison

    pred = ScanPredicate{col->column_idx, op, lit->value};
    return true;
}

// Walk an expression and collect all BoundColumnRef::column_idx values.
void CollectFilterCols(const LogicalExpr& expr, std::unordered_set<size_t>& cols) {
    switch (expr.kind) {
        case LogicalExpr::Kind::BOUND_COLUMN_REF:
            cols.insert(static_cast<const BoundColumnRef&>(expr).column_idx);
            break;
        case LogicalExpr::Kind::BINARY_OP: {
            const auto& b = static_cast<const LogicalBinaryOp&>(expr);
            if (b.left)  CollectFilterCols(*b.left,  cols);
            if (b.right) CollectFilterCols(*b.right, cols);
            break;
        }
        case LogicalExpr::Kind::UNARY_OP: {
            const auto& u = static_cast<const LogicalUnaryOp&>(expr);
            if (u.child) CollectFilterCols(*u.child, cols);
            break;
        }
        case LogicalExpr::Kind::CAST: {
            const auto& c = static_cast<const LogicalCast&>(expr);
            if (c.child) CollectFilterCols(*c.child, cols);
            break;
        }
        default:
            break;
    }
}

// Deep-clone an expression and remap BoundColumnRef::column_idx using col_map.
std::unique_ptr<LogicalExpr> CloneAndRemap(
    const LogicalExpr& expr,
    const std::unordered_map<size_t, size_t>& col_map) {
    switch (expr.kind) {
        case LogicalExpr::Kind::BOUND_COLUMN_REF: {
            const auto& ref = static_cast<const BoundColumnRef&>(expr);
            auto n = std::make_unique<BoundColumnRef>();
            n->result_type = ref.result_type;
            n->name        = ref.name;
            auto it = col_map.find(ref.column_idx);
            n->column_idx  = (it != col_map.end()) ? it->second : ref.column_idx;
            return n;
        }
        case LogicalExpr::Kind::LITERAL: {
            const auto& lit = static_cast<const LogicalLit&>(expr);
            auto n = std::make_unique<LogicalLit>();
            n->result_type = lit.result_type;
            n->value       = lit.value;
            n->is_null     = lit.is_null;
            return n;
        }
        case LogicalExpr::Kind::BINARY_OP: {
            const auto& b = static_cast<const LogicalBinaryOp&>(expr);
            auto n = std::make_unique<LogicalBinaryOp>();
            n->result_type = b.result_type;
            n->op          = b.op;
            if (b.left)  n->left  = CloneAndRemap(*b.left,  col_map);
            if (b.right) n->right = CloneAndRemap(*b.right, col_map);
            return n;
        }
        case LogicalExpr::Kind::UNARY_OP: {
            const auto& u = static_cast<const LogicalUnaryOp&>(expr);
            auto n = std::make_unique<LogicalUnaryOp>();
            n->result_type = u.result_type;
            n->op          = u.op;
            if (u.child) n->child = CloneAndRemap(*u.child, col_map);
            return n;
        }
        case LogicalExpr::Kind::CAST: {
            const auto& c = static_cast<const LogicalCast&>(expr);
            auto n = std::make_unique<LogicalCast>();
            n->result_type = c.result_type;
            n->from_type   = c.from_type;
            if (c.child) n->child = CloneAndRemap(*c.child, col_map);
            return n;
        }
        default:
            return CloneExpr(expr); // fallback for AGGREGATE etc.
    }
}

} // namespace

std::unique_ptr<OperatorState> PhysicalTableScan::CreateScanState() const {
    return std::make_unique<TableScanState>();
}

void PhysicalTableScan::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {
    // --- Zone-map predicates ---
    scan_predicates_.clear();
    for (const auto& f : pushed_filters) {
        if (!f) continue;
        ScanPredicate pred{};
        if (TryExtractPredicate(*f, pred)) {
            scan_predicates_.push_back(pred);
        }
    }

    // --- Late materialization split ---
    filter_col_ids_.clear();
    payload_col_ids_.clear();
    filter_table_col_ids_.clear();
    payload_table_col_ids_.clear();
    early_filters_.clear();

    if (!pushed_filters.empty()) {
        // Collect all chunk positions referenced by any pushed filter.
        std::unordered_set<size_t> filter_set;
        for (const auto& f : pushed_filters) {
            if (f) CollectFilterCols(*f, filter_set);
        }

        // Split column_ids into filter (early) and payload (late) — preserve order.
        for (size_t i = 0; i < column_ids.size(); ++i) {
            if (filter_set.count(i)) {
                filter_col_ids_.push_back(i);
                filter_table_col_ids_.push_back(column_ids[i]);
            } else {
                payload_col_ids_.push_back(i);
                payload_table_col_ids_.push_back(column_ids[i]);
            }
        }

        // Build remap: full-chunk position → early_chunk column index.
        std::unordered_map<size_t, size_t> full_to_early;
        for (size_t i = 0; i < filter_col_ids_.size(); ++i) {
            full_to_early[filter_col_ids_[i]] = i;
        }

        // Clone pushed_filters with column refs remapped to early_chunk positions.
        for (const auto& f : pushed_filters) {
            if (f) early_filters_.push_back(CloneAndRemap(*f, full_to_early));
        }
    }
}

OperatorResultType PhysicalTableScan::GetData(OperatorState& raw_state,
                                               DataChunk& output,
                                               ClientContext& ctx) {
    auto& state = static_cast<TableScanState&>(raw_state);

    if (!ctx.catalog || !ctx.transaction) {
        return OperatorResultType::FINISHED;
    }

    TableCatalogEntry* table = ctx.catalog->GetTable(schema_name, table_name, *ctx.transaction);
    if (!table) return OperatorResultType::FINISHED;

    auto& row_groups = table->row_groups;

    while (state.row_group_idx < row_groups.size()) {
        RowGroup* rg = row_groups[state.row_group_idx].get();

        // Zone-map: skip segments whose stats provably exclude all scan predicates.
        if (!scan_predicates_.empty()) {
            while (state.row_offset_in_group < rg->RowCount()) {
                size_t skip = rg->ZoneMapSkipRows(state.row_offset_in_group, scan_predicates_);
                if (skip == 0) break;
                state.row_offset_in_group += skip;
            }
        }

        if (state.row_offset_in_group >= rg->RowCount()) {
            ++state.row_group_idx;
            state.row_offset_in_group = 0;
            continue;
        }

        // --- Late materialization path ---
        if (!payload_col_ids_.empty()) {
            // Phase 1: scan filter columns + MVCC → early_chunk + absolute row offsets.
            DataChunk early_chunk;
            std::vector<uint32_t> vis_offsets;
            rg->ScanBatchWithOffsets(state.row_offset_in_group, filter_table_col_ids_,
                                     early_chunk, vis_offsets, *ctx.transaction);

            if (state.row_offset_in_group >= rg->RowCount()) {
                ++state.row_group_idx;
                state.row_offset_in_group = 0;
            }

            if (early_chunk.count == 0) continue;

            // Phase 2: apply pushed_filters on early_chunk → selection indices.
            std::vector<uint16_t> pred_sel;
            if (!early_filters_.empty()) {
                pred_sel = ExprEvaluator::EvaluatePredicate(*early_filters_[0], early_chunk);
                for (size_t fi = 1; fi < early_filters_.size() && !pred_sel.empty(); ++fi) {
                    auto sel2 = ExprEvaluator::EvaluatePredicate(*early_filters_[fi], early_chunk);
                    // Intersect both selections (both are sorted ascending).
                    std::vector<uint16_t> inter;
                    size_t j = 0;
                    for (uint16_t idx : pred_sel) {
                        while (j < sel2.size() && sel2[j] < idx) ++j;
                        if (j < sel2.size() && sel2[j] == idx) inter.push_back(idx);
                    }
                    pred_sel = std::move(inter);
                }
            } else {
                pred_sel.resize(early_chunk.count);
                std::iota(pred_sel.begin(), pred_sel.end(), uint16_t{0});
            }

            if (pred_sel.empty()) continue;

            // Convert to absolute row-group offsets for ScanLate.
            std::vector<uint32_t> final_offsets;
            final_offsets.reserve(pred_sel.size());
            for (uint16_t idx : pred_sel) final_offsets.push_back(vis_offsets[idx]);

            // Phase 3: scan payload columns only for selected rows.
            DataChunk late_chunk;
            rg->ScanLate(final_offsets, payload_table_col_ids_, late_chunk);

            // Assemble output: filter cols (compacted) + payload cols.
            output.Initialize(output_types);
            for (size_t i = 0; i < filter_col_ids_.size(); ++i) {
                DataVector& dst = output.columns[filter_col_ids_[i]];
                const DataVector& src = early_chunk.columns[i];
                for (uint16_t idx : pred_sel) {
                    DataVectorAppend(dst, src, idx);
                }
            }
            for (size_t i = 0; i < payload_col_ids_.size(); ++i) {
                output.columns[payload_col_ids_[i]] = std::move(late_chunk.columns[i]);
            }
            output.count = final_offsets.size();

            if (output.count > 0) return OperatorResultType::HAVE_MORE_OUTPUT;
            continue;
        }

        // --- Normal scan path ---
        size_t rows_read = rg->Scan(state.row_offset_in_group, column_ids, output, *ctx.transaction);
        (void)rows_read;

        if (state.row_offset_in_group >= rg->RowCount()) {
            ++state.row_group_idx;
            state.row_offset_in_group = 0;
        }

        if (output.count > 0) {
            return OperatorResultType::HAVE_MORE_OUTPUT;
        }
        // Empty chunk from this row group; try next
    }

    return OperatorResultType::FINISHED;
}

} // namespace cppcoldb
