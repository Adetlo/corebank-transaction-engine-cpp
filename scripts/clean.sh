#!/usr/bin/env bash
# Remove build output and, with --all, runtime state (database, logs, batch output).
#
# Usage: scripts/clean.sh [--all]
# Env:   BUILD_DIR, COREBANK_VAR
# Exit:  0 cleaned, 1 engine still running
SCRIPT_NAME=clean
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

if pid="$(engine_pid)"; then
  die "${EXIT_USAGE}" "engine is running; stop it before cleaning" "pid=${pid}"
fi
for dir in "${BUILD_DIR}" "${COREBANK_HOME}"/build-*; do
  if [[ -d "${dir}" ]]; then
    log_info "removing build directory" "dir=${dir}"
    rm -rf "${dir}"
  fi
done
if [[ "${1:-}" == "--all" ]]; then
  log_warn "removing runtime state" "dir=${COREBANK_VAR}"
  rm -rf "${COREBANK_VAR}" "${COREBANK_HOME}/.demo" "${COREBANK_HOME}/bench-results.json"
fi
log_info "clean complete"
