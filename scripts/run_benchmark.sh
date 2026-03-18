#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build}"

ROWS="${ROWS:-100000}"
WARMUP="${WARMUP:-3}"
ITERS="${ITERS:-10}"
INSERT_BATCH="${INSERT_BATCH:-500}"
DB_PATH="${DB_PATH:-:memory:}"

echo "[benchmark] configure + build"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" -j --target benchmark_cppcoldb

echo "[benchmark] run"
"${BUILD_DIR}/benchmark_cppcoldb" \
  --rows "${ROWS}" \
  --warmup "${WARMUP}" \
  --iters "${ITERS}" \
  --insert-batch "${INSERT_BATCH}" \
  --db "${DB_PATH}"
