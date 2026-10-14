#!/usr/bin/env bash
# Run the local synthetic benchmark and store the result as JSON.
#
# Usage: scripts/benchmark.sh [--transactions N] [--threads N] [--accounts N]
# Env:   BUILD_DIR, BENCH_OUT (default: bench-results.json in the repo root)
# Exit:  0 ok, 4 benchmark binary missing or failed
# Note:  numbers are a local synthetic benchmark, never a production-bank performance claim.
SCRIPT_NAME=benchmark
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

bench="${BUILD_DIR}/corebank-bench"
[[ -x "${bench}" ]] || die "${EXIT_SYSTEM}" "benchmark binary missing; run scripts/build.sh --release" "path=${bench}"
out="${BENCH_OUT:-${COREBANK_HOME}/bench-results.json}"
log_info "running local synthetic benchmark" "args=\"$*\""
"${bench}" "$@" || die "${EXIT_SYSTEM}" "benchmark failed"
"${bench}" "$@" --json >"${out}" || die "${EXIT_SYSTEM}" "benchmark (json) failed"
log_info "benchmark results written" "file=${out}"
