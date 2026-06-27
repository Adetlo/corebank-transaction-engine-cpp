#!/usr/bin/env bash
# Run reconciliation and fail loudly when the ledger does not agree with itself.
#
# Usage: scripts/reconcile.sh [YYYY-MM-DD] [--json]
# Env:   COREBANK_DB, COREBANK_LOG_DIR
# Exit:  0 reconciled, 3 reconciliation failed, 4 system error
# Logs:  $COREBANK_LOG_DIR/reconcile.log
SCRIPT_NAME=reconcile
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

require_binary
ensure_dirs
args=()
for arg in "$@"; do
  case "${arg}" in
    --json) args=(--json ${args[@]+"${args[@]}"}) ;;
    [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]) args+=(--date "${arg}") ;;
    *) die "${EXIT_USAGE}" "unexpected argument: ${arg}" ;;
  esac
done
recon_log="${COREBANK_LOG_DIR}/reconcile.log"
log_info "reconciling" "db=${COREBANK_DB}"
rc=0
"${COREBANK_BIN}" --db "${COREBANK_DB}" reconcile ${args[@]+"${args[@]}"} 2>&1 | tee -a "${recon_log}" || rc=${PIPESTATUS[0]}
if ((rc == 0)); then
  log_info "reconciliation passed"
else
  log_error "reconciliation did not pass" "exit=${rc}" "reason=\"$(describe_exit "${rc}")\"" "log=${recon_log}"
fi
exit "${rc}"
