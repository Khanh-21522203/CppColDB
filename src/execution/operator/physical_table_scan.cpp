#include "execution/operator/physical_table_scan.hpp"
#include "main/client_context.hpp"
#include "catalog/catalog.hpp"
#include "catalog/catalog_entry.hpp"
#include "transaction/transaction.hpp"
#include "storage/column/row_group.hpp"
#include "common/exception.hpp"

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

} // namespace

std::unique_ptr<OperatorState> PhysicalTableScan::CreateScanState() const {
    return std::make_unique<TableScanState>();
}

void PhysicalTableScan::InitScan(OperatorState& /*state*/, ClientContext& /*ctx*/) {
    // Extract simple col-op-literal predicates from pushed_filters for zone-map use.
    scan_predicates_.clear();
    for (const auto& f : pushed_filters) {
        if (!f) continue;
        ScanPredicate pred{};
        if (TryExtractPredicate(*f, pred)) {
            scan_predicates_.push_back(pred);
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
