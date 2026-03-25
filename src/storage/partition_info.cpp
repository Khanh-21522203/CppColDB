#include "storage/partition_info.hpp"
#include "common/exception.hpp"
#include <algorithm>
#include <numeric>

namespace cppcoldb {

// ---------------------------------------------------------------------------
// RouteRow
// ---------------------------------------------------------------------------

uint32_t PartitionInfo::RouteRow(const Value* keys, size_t nkeys) const {
    for (size_t i = 0; i < nkeys; ++i)
        if (keys[i].IsNull())
            throw RuntimeError("partition key must not be NULL");

    switch (type) {
        case PartitionType::RANGE: {
            // Find the first partition whose upper bound (lexicographic) is > keys tuple.
            // upper_bounds.empty() = open upper bound (catch-all last partition).
            for (uint32_t i = 0; i < static_cast<uint32_t>(defs.size()); ++i) {
                const auto& ub = defs[i].upper_bounds;
                if (ub.empty()) return i; // open upper bound
                // Lexicographic comparison: keys < ub?
                bool less_than_ub = false;
                size_t cols = std::min(nkeys, ub.size());
                for (size_t k = 0; k < cols; ++k) {
                    if (keys[k] < ub[k]) { less_than_ub = true;  break; }
                    if (ub[k]   < keys[k]) { less_than_ub = false; break; }
                    // equal so far — only "less" if ub has more cols (no: ub is exclusive so equal prefix also enters)
                    if (k + 1 == cols) less_than_ub = true; // prefix equal → falls in this partition
                }
                if (less_than_ub) return i;
            }
            return static_cast<uint32_t>(defs.size()) - 1;
        }

        case PartitionType::HASH: {
            size_t h = 0;
            for (size_t i = 0; i < nkeys; ++i)
                h = h * 31 + ValueHash(keys[i]);
            return static_cast<uint32_t>(h % num_partitions);
        }

        case PartitionType::LIST:
            for (uint32_t i = 0; i < static_cast<uint32_t>(defs.size()); ++i) {
                for (const auto& tuple : defs[i].list_values) {
                    bool eq = true;
                    size_t cols = std::min(nkeys, tuple.size());
                    for (size_t k = 0; k < cols && eq; ++k)
                        if (!(keys[k] == tuple[k])) eq = false;
                    if (eq) return i;
                }
            }
            throw RuntimeError(
                "INSERT: value does not match any LIST partition: " + keys[0].ToString());

        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// PrunedPartitions
// ---------------------------------------------------------------------------

std::vector<uint32_t> PartitionInfo::PrunedPartitions(
    const std::vector<ScanPredicate>& predicates) const {

    // Collect predicates that reference the leading partition key column.
    // For composite keys, only the leading column is used for pruning (prefix pruning).
    if (partition_col_idxs.empty()) {
        std::vector<uint32_t> all(num_partitions);
        std::iota(all.begin(), all.end(), 0u);
        return all;
    }
    const int leading_col_idx = partition_col_idxs[0];
    const ScanPredicate* part_preds[64];
    size_t np = 0;
    for (const auto& p : predicates) {
        if (static_cast<int>(p.col_idx) == leading_col_idx && np < 64)
            part_preds[np++] = &p;
    }

    // No predicate on partition key — scan all partitions.
    if (np == 0) {
        std::vector<uint32_t> all(num_partitions);
        std::iota(all.begin(), all.end(), 0u);
        return all;
    }

    std::vector<uint32_t> active;

    switch (type) {
        case PartitionType::RANGE: {
            const uint32_t n = static_cast<uint32_t>(defs.size());

            // --- Compute effective bounds from all predicates ---
            // eq: if any EQ predicate, only RouteRow(eq_val) survives.
            const Value* eq_val       = nullptr;
            // effective lower: tightest GE/GT bound.
            const Value* eff_lo       = nullptr;
            bool         lo_exclusive = false; // GT → exclusive, GE → inclusive
            // effective upper: tightest LE/LT bound.
            const Value* eff_hi       = nullptr;
            bool         hi_exclusive = false; // LT → exclusive, LE → inclusive

            for (size_t i = 0; i < np; ++i) {
                const auto* p = part_preds[i];
                switch (p->op) {
                    case ScanPredicateOp::EQ:
                        eq_val = &p->bound;
                        break;
                    case ScanPredicateOp::GE:
                        // Keep max; on tie, inclusive wins over exclusive (GE is looser than GT).
                        if (!eff_lo || p->bound < *eff_lo ||
                            (p->bound == *eff_lo && lo_exclusive)) {
                            // existing lo is tighter — skip
                        } else {
                            eff_lo = &p->bound; lo_exclusive = false;
                        }
                        break;
                    case ScanPredicateOp::GT:
                        // Keep max; on tie, GT (exclusive) is tighter than GE.
                        if (!eff_lo || p->bound < *eff_lo) {
                            // existing lo is tighter — skip
                        } else {
                            eff_lo = &p->bound; lo_exclusive = true;
                        }
                        break;
                    case ScanPredicateOp::LE:
                        if (!eff_hi || *eff_hi < p->bound ||
                            (p->bound == *eff_hi && hi_exclusive)) {
                            // existing hi is tighter — skip
                        } else {
                            eff_hi = &p->bound; hi_exclusive = false;
                        }
                        break;
                    case ScanPredicateOp::LT:
                        if (!eff_hi || *eff_hi < p->bound) {
                            // existing hi is tighter — skip
                        } else {
                            eff_hi = &p->bound; hi_exclusive = true;
                        }
                        break;
                }
            }

            // EQ: only one partition survives.
            if (eq_val) {
                active.push_back(RouteRow(*eq_val));
                return active;
            }

            // Binary search uses leading bound component (upper_bounds[0]) for pruning.
            // For composite keys this is a conservative/prefix-only prune — correct but
            // may retain a few extra partitions for tightly bounded composite ranges.
            auto LeadingUB = [&](uint32_t i) -> const Value* {
                const auto& ub = defs[i].upper_bounds;
                if (ub.empty()) return nullptr;          // open upper bound
                if (ub[0].IsNull()) return nullptr;      // null = open
                return &ub[0];
            };

            // Binary search for lo_pid: first partition where upper_bound > eff_lo.
            uint32_t lo_pid = 0;
            if (eff_lo) {
                uint32_t lo = 0, hi = n;
                while (lo < hi) {
                    uint32_t mid = (lo + hi) / 2;
                    const Value* ub = LeadingUB(mid);
                    // Skip partition if ub <= eff_lo (all rows < ub <= eff_lo)
                    if (ub && !((*eff_lo) < *ub)) lo = mid + 1;
                    else hi = mid;
                }
                lo_pid = lo;
            }

            // Binary search for hi_pid: last partition where lower_bound < eff_hi (LT)
            // or lower_bound <= eff_hi (LE). lower_bound of i = leading UB of defs[i-1].
            uint32_t hi_pid = n - 1;
            if (eff_hi) {
                uint32_t lo = 1, hi = n;
                while (lo < hi) {
                    uint32_t mid = (lo + hi) / 2;
                    const Value* lb = LeadingUB(mid - 1);
                    bool exclude;
                    if (hi_exclusive) {
                        exclude = lb && !((*lb) < *eff_hi);
                    } else {
                        exclude = lb && (*eff_hi < *lb);
                    }
                    if (exclude) hi = mid;
                    else lo = mid + 1;
                }
                hi_pid = (lo == 0) ? 0 : lo - 1;
            }

            if (lo_pid <= hi_pid) {
                active.reserve(hi_pid - lo_pid + 1);
                for (uint32_t i = lo_pid; i <= hi_pid; ++i) active.push_back(i);
            }
            break;
        }

        case PartitionType::HASH: {
            // For composite HASH, EQ pruning only works when ALL key columns have an
            // EQ predicate (otherwise the combined hash cannot be determined).
            const size_t nkeys = partition_col_idxs.size();
            if (nkeys == 1) {
                // Single-key: one EQ predicate is sufficient.
                const Value* eq_val = nullptr;
                for (size_t i = 0; i < np; ++i) {
                    if (part_preds[i]->op == ScanPredicateOp::EQ) {
                        eq_val = &part_preds[i]->bound;
                        break;
                    }
                }
                if (eq_val) {
                    active.push_back(RouteRow(*eq_val));
                } else {
                    active.resize(num_partitions);
                    std::iota(active.begin(), active.end(), 0u);
                }
            } else {
                // Composite: collect EQ value for each key column; prune only if all present.
                std::vector<const Value*> eq_vals(nkeys, nullptr);
                for (size_t k = 0; k < nkeys; ++k) {
                    for (const auto& p : predicates) {
                        if (static_cast<int>(p.col_idx) == partition_col_idxs[k] &&
                            p.op == ScanPredicateOp::EQ) {
                            eq_vals[k] = &p.bound;
                            break;
                        }
                    }
                }
                bool all_eq = true;
                for (size_t k = 0; k < nkeys; ++k)
                    if (!eq_vals[k]) { all_eq = false; break; }

                if (all_eq) {
                    Value keys_arr[8];
                    size_t used = std::min(nkeys, size_t{8});
                    for (size_t k = 0; k < used; ++k) keys_arr[k] = *eq_vals[k];
                    active.push_back(RouteRow(keys_arr, used));
                } else {
                    active.resize(num_partitions);
                    std::iota(active.begin(), active.end(), 0u);
                }
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
                // Find which partition contains eq_val (compare against leading key column).
                for (uint32_t i = 0; i < static_cast<uint32_t>(defs.size()); ++i) {
                    for (const auto& tuple : defs[i].list_values) {
                        if (!tuple.empty() && *eq_val == tuple[0]) { active.push_back(i); break; }
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
