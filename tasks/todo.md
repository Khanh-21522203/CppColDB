# Stabilization Fixes — Mutation/WAL/ORDER BY

## Status Legend
- [ ] not started
- [~] in progress
- [x] done

---

## Plan

- [x] Fix transaction/WAL correctness
  - [x] Move DDL WAL logging to commit-time (`Transaction::WriteToWAL`) to avoid replaying rolled-back DDL
  - [x] Fix INSERT WAL writing to use actual inserted rows (not `local_storage`)
  - [x] Implement `WAL_UPDATE` replay path
  - [x] Tighten SQL txn keyword interception (`BEGIN/COMMIT/ROLLBACK`) to reject trailing garbage

- [x] Fix UPDATE/DELETE execution correctness
  - [x] Prevent scan-loop premature stop (`break` -> `continue` when visible batch is empty)
  - [x] Make UPDATE rollback restore old values
  - [x] Make `ColumnChunk::WriteRow` support segment-backed rows
  - [x] Avoid oversized update undo chunks (> `STANDARD_VECTOR_SIZE`)

- [x] Fix ORDER BY correctness
  - [x] Restore ORDER BY handling for aggregate JOIN paths
  - [x] Bind aggregate ORDER BY against post-aggregate output schema
  - [x] Define deterministic NULL ordering in sort comparator

- [x] Fix checkpoint safety
  - [x] Keep row-group flush for persistence but skip row groups with uncommitted MVCC markers
  - [x] Skip checkpoint creation while uncommitted row version markers exist

- [x] Add regression tests and validate
  - [x] Add integration coverage for rollbacked UPDATE value restoration
  - [x] Add integration coverage for aggregate JOIN ORDER BY
  - [x] Add integration coverage for persisted-row UPDATE and strict txn keyword parsing
  - [x] Run build + full test suite

---

## Review

- Build/test: `cmake --build build -j` and `ctest --test-dir build --output-on-failure` passed (`12/12`).
- Manual repro checks passed:
  - `BEGIN; UPDATE ...; ROLLBACK; SELECT ...` restores original value.
  - Persisted-row update (after reopen) succeeds.
  - `GROUP BY ... ORDER BY ... DESC` in JOIN path returns descending order.
  - `BEGIN nonsense;` now returns parse-style error instead of being accepted.
