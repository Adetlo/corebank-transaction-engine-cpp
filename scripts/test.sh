#!/usr/bin/env bash
# Run the test suite (all categories, or a ctest label).
#
# Usage: scripts/test.sh [label] [extra ctest args]
#        labels: unit integration concurrency batch messaging property cli
# Env:   BUILD_DIR, CTEST_PARALLEL (default: 4)
# Exit:  0 all passed, 4 build missing or tests failed
SCRIPT_NAME="test"
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

require_command ctest
[[ -d "${BUILD_DIR}" ]] || die "${EXIT_SYSTEM}" "build directory missing; run scripts/build.sh" "build_dir=${BUILD_DIR}"
label_args=()
if (($# > 0)) && [[ "$1" != -* ]]; then
  label_args=(-L "^$1$")
  shift
fi
log_info "running tests" "build_dir=${BUILD_DIR}" "label=${label_args[1]:-all}"
if ctest --test-dir "${BUILD_DIR}" --output-on-failure -j "${CTEST_PARALLEL:-4}" ${label_args[@]+"${label_args[@]}"} "$@"; then
  log_info "tests passed"
else
  die "${EXIT_SYSTEM}" "tests failed"
fi
