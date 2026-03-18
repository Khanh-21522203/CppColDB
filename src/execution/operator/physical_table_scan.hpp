#pragma once
#include "execution/physical_operator.hpp"
#include "execution/pipeline.hpp"
#include "planner/logical_plan/logical_plan.hpp"
#include "storage/scan_predicate.hpp"

namespace cppcoldb {

struct TableScanState : OperatorState {
    size_t row_group_idx       = 0;
    size_t row_offset_in_group = 0;
};

struct PhysicalTableScan : PhysicalOperator {
    std::string schema_name;
    std::string table_name;
    std::vector<size_t> column_ids;
    std::vector<std::unique_ptr<LogicalExpr>> pushed_filters;
    // Extracted zone-map predicates (populated by InitScan from pushed_filters).
    std::vector<ScanPredicate> scan_predicates_;
    // Late materialization fields (populated by InitScan).
    // When payload_col_ids_ is non-empty, GetData uses a two-phase scan:
    //   phase 1: scan filter columns + apply predicates inline → selection
    //   phase 2: scan payload columns only for selected rows (ScanLate)
    std::vector<size_t> filter_col_ids_;         // chunk positions of predicate columns
    std::vector<size_t> payload_col_ids_;        // chunk positions of remaining columns
    std::vector<size_t> filter_table_col_ids_;   // table-level col indices for filter cols
    std::vector<size_t> payload_table_col_ids_;  // table-level col indices for payload cols
    // pushed_filters cloned and remapped to early_chunk column positions (0..filter_col_ids_.size()-1)
    std::vector<std::unique_ptr<LogicalExpr>> early_filters_;

    PhysicalTableScan() { role = OperatorRole::SOURCE; }

    std::unique_ptr<OperatorState> CreateScanState() const override;
    void               InitScan(OperatorState& state, ClientContext& ctx) override;
    OperatorResultType GetData (OperatorState& state, DataChunk& output,
                                ClientContext& ctx) override;
};

} // namespace cppcoldb
