#include "storage/partition_info.hpp"
#include "common/exception.hpp"
#include <numeric>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// RouteRow
// ---------------------------------------------------------------------------

uint32_t PartitionInfo::RouteRow(const Value& key) const {
    switch (type) {
        case PartitionType::RANGE:
            // defs[i].upper_bound is exclusive. Find the first partition whose
            // upper_bound is null (last partition, catch-all) or > key.
            for (uint32_t i = 0; i < static_cast<uint32_t>(defs.size()); ++i) {
                if (defs[i].upper_bound.IsNull() || key < defs[i].upper_bound)
                    return i;
            }
            // Shouldn't reach here if defs is well-formed (last entry has IsNull upper_bound).
            return static_cast<uint32_t>(defs.size()) - 1;

        case PartitionType::HASH:
            return static_cast<uint32_t>(ValueHash(key) % num_partitions);

        case PartitionType::LIST:
            for (uint32_t i = 0; i < static_cast<uint32_t>(defs.size()); ++i) {
                for (const auto& v : defs[i].list_values) {
                    if (key == v) return i;
                }
            }
            throw RuntimeError(
                "INSERT: value does not match any LIST partition: " + key.ToString());

        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// Partition pruning helpers
// ---------------------------------------------------------------------------

// Returns true if a RANGE partition [lower, upper) could contain a row matching op bound.
// lower.IsNull() means -inf; upper.IsNull() means +inf.
static bool RangePartitionCouldMatch(
    const Value& lower, const Value& upper,
    ScanPredicateOp op, const Value& bound) {

    switch (op) {
        case ScanPredicateOp::EQ:
            // Need lower <= bound < upper
            if (!lower.IsNull() && bound < lower) return false;
            if (!upper.IsNull() && !(bound < upper)) return false;
            return true;

        case ScanPredicateOp::LT:
            // Need some row < bound, i.e. lower < bound
            if (!lower.IsNull() && !(lower < bound)) return false;
            return true;

        case ScanPredicateOp::LE:
            // Need some row <= bound, i.e. lower <= bound
            if (!lower.IsNull() && !(lower < bound) && !(lower == bound)) return false;
            return true;

        case ScanPredicateOp::GT:
            // Need some row > bound; partition rows are < upper (exclusive).
            // Skip if upper <= bound, i.e. !(upper > bound) = !(bound < upper) = bound >= upper.
            if (!upper.IsNull() && !(bound < upper)) return false;
            return true;

        case ScanPredicateOp::GE:
            // Need some row >= bound; skip if upper <= bound (all rows < upper <= bound).
            if (!upper.IsNull() && !(bound < upper)) return false;
            return true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// PrunedPartitions
// ---------------------------------------------------------------------------

std::vector<uint32_t> PartitionInfo::PrunedPartitions(
    const std::vector<ScanPredicate>& predicates) const {

    // Collect predicates that reference the partition column.
    std::vector<const ScanPredicate*> part_preds;
    for (const auto& p : predicates) {
        if (static_cast<int>(p.col_idx) == partition_col_idx) {
            part_preds.push_back(&p);
        }
    }

    // No predicate on partition key — scan all partitions.
    if (part_preds.empty()) {
        std::vector<uint32_t> all(num_partitions);
        std::iota(all.begin(), all.end(), 0u);
        return all;
    }

    std::vector<uint32_t> active;

    switch (type) {
        case PartitionType::RANGE: {
            for (uint32_t i = 0; i < static_cast<uint32_t>(defs.size()); ++i) {
                // lower bound of partition i
                const Value& lower = (i == 0) ? Value::Null() : defs[i - 1].upper_bound;
                const Value& upper = defs[i].upper_bound;

                bool keep = true;
                for (const auto* p : part_preds) {
                    if (!RangePartitionCouldMatch(lower, upper, p->op, p->bound)) {
                        keep = false;
                        break;
                    }
                }
                if (keep) active.push_back(i);
            }
            break;
        }

        case PartitionType::HASH: {
            // Only EQ predicate allows pruning to a single bucket.
            bool has_eq = false;
            const Value* eq_val = nullptr;
            for (const auto* p : part_preds) {
                if (p->op == ScanPredicateOp::EQ) {
                    has_eq   = true;
                    eq_val   = &p->bound;
                    break;
                }
            }
            if (has_eq && eq_val) {
                active.push_back(RouteRow(*eq_val));
            } else {
                active.resize(num_partitions);
                std::iota(active.begin(), active.end(), 0u);
            }
            break;
        }

        case PartitionType::LIST: {
            // Only EQ predicate allows single-partition pruning.
            bool has_eq = false;
            const Value* eq_val = nullptr;
            for (const auto* p : part_preds) {
                if (p->op == ScanPredicateOp::EQ) {
                    has_eq  = true;
                    eq_val  = &p->bound;
                    break;
                }
            }
            if (has_eq && eq_val) {
                // Find which partition contains eq_val.
                for (uint32_t i = 0; i < static_cast<uint32_t>(defs.size()); ++i) {
                    for (const auto& v : defs[i].list_values) {
                        if (*eq_val == v) { active.push_back(i); break; }
                    }
                }
                // If not found in any partition, return empty (no rows can match).
            } else {
                active.resize(num_partitions);
                std::iota(active.begin(), active.end(), 0u);
            }
            break;
        }

        default:
            active.resize(num_partitions);
            std::iota(active.begin(), active.end(), 0u);
    }

    return active;
}

} // namespace cppcoldb
